#include "key_task.h"
#include "heat_task.h"
#include "key.h"

#include "FreeRTOS.h"
#include "at_ctrl.h"
#include "task.h"

#define SCAN_INTERVAL_MS       20
#define LONG_PRESS_THRESHOLD   (2000 / SCAN_INTERVAL_MS) // 2000ms / 20ms = 100
#define DOUBLE_CLICK_THRESHOLD (500 / SCAN_INTERVAL_MS)  // 500ms / 20ms = 25

static unsigned char key_state = KEY_NONE;
static unsigned char key_counter = 0;

void key_scan(void *arg)
{
    (void) arg;
    static TickType_t xLastWakeTime = 0;
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
    // 根据事件类型执行相应操作
    switch (event)
    {
        case KEY_SHORT_PRESS:
            switch (key_id)
            {
                case KEY_MUSIC:
                    bt_mode(BTMODE_MUSIC);
                    break;
                case KEY_BLUETOOTH:
                    bt_mode(BTMODE_BT);
                    break;
                case KEY_PLAY_PAUSE:
                    music_switch();
                    break;
                case KEY_PREV:
                    music_prev();
                    break;
                case KEY_NEXT:
                    music_next();
                    break;
                case KEY_VOL_DOWN:
                    music_volume_control(VOLUME_DOWN);
                    break;
                case KEY_VOL_UP:
                    music_volume_control(VOLUME_UP);
                    break;
                case KEY_HEAT_PLUS:
                    heat_level_down();
                    break;
                case KEY_HEAT_MINUS:
                    heat_level_up();
                    break;

                case KEY_MIN10:
                    heat_set_timer(10);
                    break;
                case KEY_MIN30:
                    heat_set_timer(30);
                    break;
                case KEY_MIN60:
                    heat_set_timer(60);
                    break;
                case KEY_HEAT:
                    heat_status_switch();
                    break;
                case KEY_SHORTCUT_1:
                    // 短按处理
                    break;
                case KEY_SHORTCUT_2:
                    // 短按处理
                    break;
                case KEY_POWER:
                    // 短按处理
                    break;
                default:
                    break;
            }
            break;
        case KEY_LONG_PRESS:
            switch (key_id)
            {
                case KEY_MUSIC:
                    // 长按处理
                    break;
                case KEY_BLUETOOTH:
                    // 长按处理
                    break;
                case KEY_PLAY_PAUSE:
                    // 长按处理
                    break;
                case KEY_MIN10:
                    // 长按处理
                    break;
                case KEY_MIN60:
                    // 长按处理
                    break;
                case KEY_PREV:
                    // 长按处理
                    break;
                case KEY_NEXT:
                    // 长按处理
                    break;
                case KEY_VOL_DOWN:
                    // 长按处理
                    break;
                case KEY_VOL_UP:
                    // 长按处理
                    break;
                case KEY_HEAT_PLUS:
                    // 长按处理
                    break;
                case KEY_HEAT_MINUS:
                    // 长按处理
                    break;
                case KEY_SHORTCUT_1:
                    // 长按处理
                    break;
                case KEY_SHORTCUT_2:
                    // 长按处理
                    break;
                case KEY_MIN30:
                    // 长按处理
                    break;
                case KEY_HEAT:
                    // 长按处理
                    break;
                case KEY_POWER:
                    // 长按处理
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}
