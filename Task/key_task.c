#include "key_task.h"
#include "key.h"

#include "FreeRTOS.h"
#include "task.h"

#define SCAN_INTERVAL_MS       20
#define LONG_PRESS_THRESHOLD   (2000 / SCAN_INTERVAL_MS) // 2000ms / 20ms = 100
#define DOUBLE_CLICK_THRESHOLD (500 / SCAN_INTERVAL_MS)  // 500ms / 20ms = 25

static unsigned char key_state = KEY_NONE;
static unsigned char key_counter = 0;

void key_scan(void *arg)
{
    (void) arg;

    // 【关键】定义上次唤醒时间变量（必须是 static 或全局）
    static TickType_t xLastWakeTime = 0;

    // 初始化 xLastWakeTime 为当前时间（首次调用前）
    xLastWakeTime = xTaskGetTickCount();

    static unsigned char before = 0;

    for (;;)
    {
        unsigned char now = get_key();

        if (before == 0 && now != 0)
        {
            // 按键按下
            key_counter = 0;
            key_state = KEY_NONE;
        }
        else if (before != 0 && now == 0)
        {
            // 按键释放
            if (key_state == KEY_NONE)
            {
                if (key_counter < LONG_PRESS_THRESHOLD)
                {
                    // 短按事件
                    key_event_handler(before, KEY_SHORT_PRESS);
                }
                // 可在此处添加双击逻辑（需记录上次释放时间）
            }
            key_counter = 0;
        }
        else if (now != 0)
        {
            // 按键持续按下
            key_counter++;
            if (key_counter >= LONG_PRESS_THRESHOLD && key_state == KEY_NONE)
            {
                key_state = KEY_LONG_PRESS;
                key_event_handler(now, KEY_LONG_PRESS);
            }
        }

        before = now;

        // 每 20ms 执行一次，自动补偿处理时间
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SCAN_INTERVAL_MS));
    }
}

void key_event_handler(uint8_t key_id, key_event_t event)
{
    // 根据按键 ID 和事件类型执行相应操作
    switch (key_id)
    {
        case KEY_ZHUMIAN:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_BLUETOOTH:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_PLAY_PAUSE:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_MIN10:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_MIN60:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_PREV:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_NEXT:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_VOL_DOWN:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_VOL_UP:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_HEAT_PLUS:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_HEAT_MINUS:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_SHORTCUT_1:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_SHORTCUT_2:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_MIN30:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_HEAT:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        case KEY_POWER:
            if (event == KEY_SHORT_PRESS) {}
            else if (event == KEY_LONG_PRESS) {}
            break;
        default:
            break;
    }
}
