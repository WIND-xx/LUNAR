/**
 * @file    bsp_ntc.c
 * @brief   NTC 温度传感器驱动实现（静态分配，查表 + DMA + 滤波）
 * @version 2.1
 */

#include "bsp_ntc.h"

/*==============================================================================
 * 内部常量
 *============================================================================*/

#define ADC_MAX_VALUE 4095
#define ADC_DMA_BUF_SIZE 16
#define TEMP_MIN_VALID (-20.0f)
#define TEMP_MAX_VALID 100.0f
#define STUCK_THRESHOLD 10

/* 查表数据 — Flash 驻留 */
static const uint16_t ntc_adc_table[] = {
    3996, 3988, 3981, 3972, 3964, 3955, 3945, 3935, 3924, 3912, 3900, 3887, 3874, 3860, 3845, 3830, 3813, 3796,
    3778, 3760, 3740, 3720, 3698, 3676, 3653, 3629, 3604, 3578, 3551, 3524, 3495, 3465, 3435, 3403, 3371, 3337,
    3303, 3267, 3231, 3194, 3156, 3118, 3078, 3038, 2997, 2955, 2913, 2870, 2826, 2782, 2738, 2693, 2648, 2602,
    2556, 2510, 2464, 2417, 2371, 2324, 2278, 2231, 2185, 2139, 2093, 2048, 2002, 1957, 1913, 1868, 1825, 1781,
    1739, 1697, 1655, 1614, 1574, 1534, 1495, 1456, 1419, 1382, 1346, 1310, 1275, 1241, 1208, 1175, 1143, 1112,
    1081, 1052, 1023, 994,  967,  940,  914,  888,  863,  839,  815,  792,  770,  748,  727,  707,  687,  668,
    649,  631,  613,  596,  579,  563,  547,  532,  517,  502,  488,  475,  462,  449,  436,  424,  413,  401,
    390,  380,  369,  359,  350,  340,  331,  322,  314,  305,  297,  289,  282,  274,  267,  260,  253,  247,
    240,  234,  228,  222,  217,  211,  206,  201,  196,  191,  186,  181,  177,  173,  168,  164,  160};

static const int8_t ntc_temp_table[] = {
    -40, -39, -38, -37, -36, -35, -34, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18,
    -17, -16, -15, -14, -13, -12, -11, -10, -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,   1,   2,   3,   4,   5,
    6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,
    29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,
    52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,
    75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,
    98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120};
#define NTC_TABLE_SIZE BSP_ARRAY_SIZE(ntc_adc_table)

/*==============================================================================
 * 内部结构体（静态单例）
 *============================================================================*/

struct bsp_ntc_s {
    ADC_HandleTypeDef* hadc;
    float temp_offset;
    /* 滤波 */
    uint32_t adc_moving_buf[NTC_MOVING_AVG_LEN];
    uint8_t moving_idx;
    uint32_t moving_sum;
    float last_filtered_temp;
    bool is_first_sample;
    /* 故障检测 */
    uint32_t last_raw_adc;
    uint8_t stuck_counter;
    bool fault_active;
    /* DMA */
#if NTC_USE_DMA
#if USE_DOUBLE_BUFFER
    uint32_t dma_buf[2][ADC_DMA_BUF_SIZE];
    volatile uint8_t active_buf;
    volatile uint8_t data_ready;
#else
    uint32_t dma_buf[ADC_DMA_BUF_SIZE];
#endif
#endif
    bool initialized;
};

static struct bsp_ntc_s s_inst;
static bool s_inited = false;

#if NTC_USE_DMA && USE_DOUBLE_BUFFER
bsp_ntc_t* g_ntc_instance = NULL;
#endif

/*==============================================================================
 * 内部 — 中值滤波
 *============================================================================*/

static uint32_t ntc_median(uint32_t buf[], uint8_t n) {
    uint32_t tmp[NTC_FILTER_SAMPLES];
    /* 手工拷贝代替 memcpy（省 ROM） */
    for (uint8_t i = 0; i < n; i++) tmp[i] = buf[i];
    for (uint8_t i = 0; i < n - 1; i++) {
        bool swapped = false;
        for (uint8_t j = 0; j < n - i - 1; j++) {
            if (tmp[j] > tmp[j + 1]) {
                uint32_t t = tmp[j];
                tmp[j]     = tmp[j + 1];
                tmp[j + 1] = t;
                swapped    = true;
            }
        }
        if (!swapped) break;
    }
    return tmp[n / 2];
}

/*==============================================================================
 * 内部 — 滑动平均
 *============================================================================*/

static uint32_t ntc_moving_avg(uint32_t new_val) {
    s_inst.moving_sum -= s_inst.adc_moving_buf[s_inst.moving_idx];
    s_inst.moving_sum += new_val;
    s_inst.adc_moving_buf[s_inst.moving_idx] = new_val;
    s_inst.moving_idx                        = (s_inst.moving_idx + 1) & (NTC_MOVING_AVG_LEN - 1);
#if NTC_MOVING_AVG_LEN == 8
    return s_inst.moving_sum >> 3;
#elif NTC_MOVING_AVG_LEN == 4
    return s_inst.moving_sum >> 2;
#elif NTC_MOVING_AVG_LEN == 16
    return s_inst.moving_sum >> 4;
#else
    return s_inst.moving_sum / NTC_MOVING_AVG_LEN;
#endif
}

/*==============================================================================
 * 内部 — 插值查表
 *============================================================================*/

static float ntc_interpolate(uint32_t adc) {
    static uint8_t last_idx = 0;
    if (adc >= ntc_adc_table[0]) return -40.0f;
    if (adc <= ntc_adc_table[NTC_TABLE_SIZE - 1]) return 120.0f;

    uint8_t i = last_idx;
    if (adc < ntc_adc_table[i]) {
        while (i < NTC_TABLE_SIZE - 1 && adc < ntc_adc_table[i + 1]) i++;
    } else if (adc > ntc_adc_table[i]) {
        while (i > 0 && adc > ntc_adc_table[i - 1]) i--;
        if (i > 0) i--;
    }
    last_idx = i;

    float ratio = (float)(ntc_adc_table[i] - adc) / (float)(ntc_adc_table[i] - ntc_adc_table[i + 1]);
    return (float)ntc_temp_table[i] + ratio * (float)(ntc_temp_table[i + 1] - ntc_temp_table[i]);
}

/*==============================================================================
 * 内部 — 低通滤波
 *============================================================================*/

static float ntc_low_pass(float new_val, float last_val) {
    return NTC_LOW_PASS_ALPHA * new_val + (1.0f - NTC_LOW_PASS_ALPHA) * last_val;
}

/*==============================================================================
 * 内部 — 故障检测
 *============================================================================*/

static bool ntc_detect_fault(uint32_t adc_val) {
    if (adc_val >= ADC_MAX_VALUE - 10) {
        s_inst.fault_active = true;
        return true;
    }
    if (adc_val <= 10) {
        s_inst.fault_active = true;
        return true;
    }
    if (adc_val == s_inst.last_raw_adc) {
        if (++s_inst.stuck_counter >= STUCK_THRESHOLD) {
            s_inst.fault_active = true;
            return true;
        }
    } else {
        s_inst.stuck_counter = 0;
        s_inst.last_raw_adc  = adc_val;
    }
    s_inst.fault_active = false;
    return false;
}

/*==============================================================================
 * DMA ISR 回调
 *============================================================================*/

#if NTC_USE_DMA && USE_DOUBLE_BUFFER
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    if (!g_ntc_instance || hadc->Instance != ADC1) return;
    g_ntc_instance->active_buf ^= 1;
    g_ntc_instance->data_ready = 1;
    HAL_ADC_Stop_DMA(hadc);
    HAL_ADC_Start_DMA(hadc, (uint32_t*)g_ntc_instance->dma_buf[g_ntc_instance->active_buf], ADC_DMA_BUF_SIZE);
}

static int ntc_dma_extract(uint32_t* dest, uint8_t n) {
    if (!s_inst.data_ready) return -1;
    uint8_t ready_buf = s_inst.active_buf ^ 1;
    __disable_irq();
    for (uint8_t i = 0; i < n; i++) {
        dest[i] = s_inst.dma_buf[ready_buf][(ADC_DMA_BUF_SIZE - n + i) & (ADC_DMA_BUF_SIZE - 1)];
    }
    s_inst.data_ready = 0;
    __enable_irq();
    return 0;
}
#elif NTC_USE_DMA
static int ntc_dma_extract(uint32_t* dest, uint8_t n) {
    __disable_irq();
    HAL_ADC_Stop_DMA(s_inst.hadc);
    for (uint8_t i = 0; i < n; i++) dest[i] = s_inst.dma_buf[(ADC_DMA_BUF_SIZE - n + i) % ADC_DMA_BUF_SIZE];
    HAL_ADC_Start_DMA(s_inst.hadc, s_inst.dma_buf, ADC_DMA_BUF_SIZE);
    __enable_irq();
    return 0;
}
#endif

/*==============================================================================
 * 内部 — 采样 + 滤波
 *============================================================================*/

static int ntc_sample(uint32_t* adc_out) {
    uint32_t raw[NTC_FILTER_SAMPLES];
#if NTC_USE_DMA
    if (ntc_dma_extract(raw, NTC_FILTER_SAMPLES) != 0) {
        *adc_out = ADC_MAX_VALUE / 2;
        return -1;
    }
#else
    HAL_ADC_Start(s_inst.hadc);
    for (uint8_t i = 0; i < NTC_FILTER_SAMPLES; i++) {
        raw[i] =
            (HAL_ADC_PollForConversion(s_inst.hadc, 2) == HAL_OK) ? HAL_ADC_GetValue(s_inst.hadc) : ADC_MAX_VALUE / 2;
    }
    HAL_ADC_Stop(s_inst.hadc);
#endif
    *adc_out = ntc_moving_avg(ntc_median(raw, NTC_FILTER_SAMPLES));
    return 0;
}

/*==============================================================================
 * 公共 API
 *============================================================================*/

bsp_status_t bsp_ntc_init(bsp_ntc_t** handle, const bsp_ntc_config_t* config) {
    if (!handle || !config || !config->hadc) return BSP_ERR_PARAM;
    if (*handle || s_inited) return BSP_ERR_BUSY;

    s_inst.hadc        = config->hadc;
    s_inst.temp_offset = config->temp_offset;
    for (uint8_t i = 0; i < NTC_MOVING_AVG_LEN; i++) s_inst.adc_moving_buf[i] = ADC_MAX_VALUE / 2;
    s_inst.moving_sum         = (ADC_MAX_VALUE / 2) * NTC_MOVING_AVG_LEN;
    s_inst.moving_idx         = 0;
    s_inst.last_filtered_temp = 25.0f;
    s_inst.is_first_sample    = true;
    s_inst.fault_active       = false;
    s_inst.stuck_counter      = 0;

#if NTC_USE_DMA
#if USE_DOUBLE_BUFFER
    for (uint8_t i = 0; i < 2; i++)
        for (uint8_t j = 0; j < ADC_DMA_BUF_SIZE; j++) s_inst.dma_buf[i][j] = ADC_MAX_VALUE / 2;
    s_inst.active_buf = 0;
    s_inst.data_ready = 0;
    g_ntc_instance    = &s_inst;
    HAL_ADC_Start_DMA(s_inst.hadc, (uint32_t*)s_inst.dma_buf[0], ADC_DMA_BUF_SIZE);
#else
    for (uint8_t i = 0; i < ADC_DMA_BUF_SIZE; i++) s_inst.dma_buf[i] = ADC_MAX_VALUE / 2;
    HAL_ADC_Start_DMA(s_inst.hadc, s_inst.dma_buf, ADC_DMA_BUF_SIZE);
#endif
#endif

    s_inst.initialized = true;
    s_inited           = true;
    *handle            = (bsp_ntc_t*)&s_inst;
    return BSP_OK;
}

void bsp_ntc_deinit(bsp_ntc_t** handle) {
    if (!handle || !*handle || !s_inited) return;
#if NTC_USE_DMA
    HAL_ADC_Stop_DMA(s_inst.hadc);
#if USE_DOUBLE_BUFFER
    if (g_ntc_instance == &s_inst) g_ntc_instance = NULL;
#endif
#endif
    s_inst.initialized = false;
    s_inited           = false;
    *handle            = NULL;
}

bsp_status_t bsp_ntc_read(bsp_ntc_t* handle, float* temp_c) {
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    if (!temp_c) return BSP_ERR_PARAM;

    uint32_t adc_val;
    if (ntc_sample(&adc_val) != 0) {
        *temp_c = s_inst.is_first_sample ? 25.0f : s_inst.last_filtered_temp;
        return BSP_ERROR;
    }
    if (adc_val > ADC_MAX_VALUE) adc_val = ADC_MAX_VALUE;

    if (ntc_detect_fault(adc_val)) {
        *temp_c = s_inst.last_filtered_temp;
        return BSP_ERR_HW;
    }

    float raw = ntc_interpolate(adc_val) + s_inst.temp_offset;
    float filtered;
    if (s_inst.is_first_sample) {
        s_inst.is_first_sample = false;
        filtered = s_inst.last_filtered_temp = raw;
    } else {
        filtered                  = ntc_low_pass(raw, s_inst.last_filtered_temp);
        s_inst.last_filtered_temp = filtered;
    }

    if (filtered <= TEMP_MIN_VALID || filtered >= TEMP_MAX_VALID) {
        *temp_c = s_inst.last_filtered_temp;
        return BSP_ERR_HW;
    }
    *temp_c = filtered;
    return BSP_OK;
}

bsp_status_t bsp_ntc_get_adc(bsp_ntc_t* handle, uint32_t* adc_val) {
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    if (!adc_val) return BSP_ERR_PARAM;
    return (ntc_sample(adc_val) == 0) ? BSP_OK : BSP_ERROR;
}

bool bsp_ntc_is_fault(bsp_ntc_t* handle) {
    return (handle && s_inited) ? s_inst.fault_active : false;
}

void bsp_ntc_clear_fault(bsp_ntc_t* handle) {
    if (!handle || !s_inited) return;
    s_inst.fault_active  = false;
    s_inst.stuck_counter = 0;
}

void bsp_ntc_set_offset(bsp_ntc_t* handle, float offset) {
    if (handle && s_inited) s_inst.temp_offset = offset;
}

void bsp_ntc_reset_filter(bsp_ntc_t* handle) {
    if (!handle || !s_inited) return;
    s_inst.last_filtered_temp = 25.0f;
    s_inst.is_first_sample    = true;
    s_inst.moving_idx         = 0;
    for (uint8_t i = 0; i < NTC_MOVING_AVG_LEN; i++) s_inst.adc_moving_buf[i] = ADC_MAX_VALUE / 2;
    s_inst.moving_sum = (ADC_MAX_VALUE / 2) * NTC_MOVING_AVG_LEN;
}
