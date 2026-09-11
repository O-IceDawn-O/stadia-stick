/********************************************
 * Stick — CH592F Stadia USB Dongle
 * BLE Central + USB CDC/HID
 ********************************************/

#include <stdio.h>
#include <string.h>
#ifdef CFG_CH582
#include "CH58x_common.h"
#include "CH58x_gpio.h"
#include "CH58x_sys.h"
#include "CH58x_pwr.h"
#else
#include "CH59x_common.h"
#include "CH59x_gpio.h"
#include "CH59x_sys.h"
#include "CH59x_pwr.h"
#endif
#include "CONFIG.h"
#include "HAL.h"
#include "sinput_usb.h"
#include "sinput_builder.h"
#include "button.h"
#include "platform.h"
#include "log.h"
#include "stadia_ble.h"
#include "stadia_protocol.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

/* ---- SysTick 轮询 ---- */
#define TICKS_PER_MS    60000
static volatile uint32_t g_last_systick = 0;
volatile uint32_t g_sys_tick_ms = 0;

static void update_tick(void)
{
    /*
     * SysTick->CNT 是 32-bit 自由运行计数器, 每 ~71.6s 从 0xFFFFFFFF 绕回 0.
     * 绕回后 now < g_last_systick, 直接用 now - g_last_systick 会 uint32_t 下溢.
     * 正确公式: elapsed = (2^32 - g_last_systick) + now, 等价于:
     *   (0xFFFFFFFF - g_last_systick) + now + 1
     */
    uint32_t now = (uint32_t)SysTick->CNT;
    uint32_t elapsed;
    if (now >= g_last_systick) {
        elapsed = now - g_last_systick;
    } else {
        elapsed = (0xFFFFFFFF - g_last_systick) + now + 1;
    }
    if (elapsed >= TICKS_PER_MS) {
        g_sys_tick_ms += elapsed / TICKS_PER_MS;
        g_last_systick = now - (elapsed % TICKS_PER_MS);
    }
}

/* ---- SInput Feature Request (SDL: 写 OUT [SINPUT_REPORT_ID_OUTPUT, SINPUT_CMD_FEATURES] → 回复 IN [SINPUT_REPORT_ID_FEATURES, ...]) ---- */
bool g_feature_req_pending = false;

/* ---- 震动缓存 (USB回调存, 主循环发, 解耦 BLE GATT 操作) ---- */
static uint8_t  g_rumble_left = 0;
static uint8_t  g_rumble_right = 0;
static bool     g_rumble_pending = false;
/* 震动背压: 指数退避 (写失败时加倍等待, 避免刷屏) */
static uint32_t s_rumble_retry_at = 0;
static uint16_t s_rumble_backoff = 0;
/* 连续 bleTimeout 计数: 超过阈值则断连重连恢复 ATT 状态 */
#define RUMBLE_TIMEOUT_LIMIT  50
static uint8_t s_rumble_timeout_cnt = 0;

/* ---- USB HID 输出回调 (只缓存, 不直接发 BLE) ---- */
static void on_hid_output(uint8_t *data, uint16_t len)
{
    /* Feature Request: data[0]=SINPUT_REPORT_ID_OUTPUT, data[1]=SINPUT_CMD_FEATURES */
    if (len >= 2 && data[0] == SINPUT_REPORT_ID_OUTPUT && data[1] == SINPUT_CMD_FEATURES) {
        LOG_INFO("HID", "Feature request from SDL");
        g_feature_req_pending = true;
        return;
    }
    /* 震动命令: 只缓存, 主循环发 (避免 GATT 操作冲突) */
    uint8_t left, right;
    if (sinput_parse_haptic(data, len, &left, &right)) {
        LOG_DEBUG("HID", "Rumble queued L=%u R=%u", left, right);
        g_rumble_left = left;
        g_rumble_right = right;
        g_rumble_pending = true;
        s_rumble_retry_at = 0;   /* 新数据到达, 重置退避立即重试 */
        s_rumble_backoff = 0;
        s_rumble_timeout_cnt = 0;
    }
}

/* ---- BLE 状态回调 ---- */
void StadiaBLE_OnConnected(uint16_t conn_handle)
{
    LOG_INFO("BLE", "Controller connected, handle=%d", conn_handle);
}

void StadiaBLE_OnDisconnected(uint16_t conn_handle)
{
    LOG_INFO("BLE", "Controller disconnected");
    g_rumble_pending = false;   /* 断开后清除待发震动 */
    (void)conn_handle;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = g_sys_tick_ms;
    while ((g_sys_tick_ms - start) < ms) {
        update_tick();
    }
}

int main(void)
{
    PWR_DCDCCfg(ENABLE);
    SetSysClock(CLK_SOURCE_PLL_60MHz);

    GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeOut_PP_5mA);
    platform_led_on();

    /* === USB 先 (日志) === */
    log_init();
    Button_Init();
    SInputUSB_Init();
    SInputUSB_SetOutputCallback(on_hid_output);

    /* SysTick 配置 (给 delay_ms 和日志时间戳用) */
    SysTick_Config(SysTick_LOAD_RELOAD_Msk);
    PFIC_DisableIRQ(SysTick_IRQn);
    g_last_systick = (uint32_t)SysTick->CNT;

    delay_ms(200); /* 等 USB 枚举 */
    CDC_Flush();

    LOG_INFO("MAIN", "=== BLE Central init ===");
    CDC_Flush();

    /* === BLE 协议栈 (Central) === */
#ifdef CFG_CH582
    CH58X_BLEInit();
#else
    CH59x_BLEInit();
#endif
    /* BLEInit 内部调用 SysTick_Config 重置了计数器, 重新同步基准 */
    g_last_systick = (uint32_t)SysTick->CNT;
    LOG_INFO("MAIN", "CH59x_BLEInit OK");
    CDC_Flush();

    HAL_Init();
    LOG_INFO("MAIN", "HAL_Init OK");
    CDC_Flush();

    /* 看门狗: BLE 初始化完成后使能, 避开 BLE 慢启动窗口 */
    platform_wdog_init();

    GAPRole_CentralInit();
    LOG_INFO("MAIN", "GAPRole_CentralInit OK");
    CDC_Flush();

    StadiaBLE_Init();
    LOG_INFO("MAIN", "StadiaBLE_Init OK, press BOOT to scan");
    CDC_Flush();

    /* === 主循环 === */
    uint32_t last_led = 0;

    while (1) {
        update_tick();
        platform_wdog_feed();
        uint32_t now = g_sys_tick_ms;

        CDC_Flush();

        /* 处理 HID 输出报告 (震动/Feature请求) */
        SInputUSB_ProcessOutput();

        /* 按钮: BOOT 控制扫描 (仅 BLE 就绪后生效) */
        Button_Task();
        if (Button_GetEvent()) {
            if (!g_ble_ready) {
                LOG_WARN("BTN", "BLE not ready yet");
                CDC_Flush();
            } else if (g_stadia_connected) {
                LOG_INFO("BTN", "BOOT: disconnect + scan");
                StadiaBLE_Disconnect();
                StadiaBLE_StartScan();
            } else if (StadiaBLE_IsScanning()) {
                LOG_INFO("BTN", "BOOT: stop scan");
                StadiaBLE_StopScan();
            } else {
                LOG_INFO("BTN", "BOOT: start scan");
                StadiaBLE_StartScan();
            }
            CDC_Flush();
        }

        /* SInput Feature 响应: 当 SDL 请求能力时, 发送 SINPUT_REPORT_ID_FEATURES 报告 */
        if (g_feature_req_pending) {
            uint8_t uid[6];
            platform_get_unique_id(uid);
            uint8_t resp[63];
            sinput_build_feature_response(uid, resp);
            uint8_t report[64];
            report[0] = SINPUT_REPORT_ID_FEATURES;
            memcpy(report + 1, resp, 63);
            if (!SInputUSB_SendReport(report, 64)) {
                /* EP3 忙: 保留 pending 标志, 下一轮主循环重试 (同输入报告 ready 机制) */
                LOG_WARN("HID", "Feature response dropped (EP3 busy)");
            } else {
                g_feature_req_pending = false;  /* 发送成功才清除 */
                LOG_INFO("HID", "Feature response sent");
            }
            CDC_Flush();
        }

        /* LED: 初始化=常亮, IDLE=慢闪 500ms, 扫描=快闪 100ms, 已连接=灭 */
        {
            static uint8_t led_state = 99;  /* 初始值确保首次必然更新 */
            uint8_t want;
            if (!g_ble_ready)                want = 3;  /* 初始化: 常亮 */
            else if (g_stadia_connected)     want = 2;  /* 已连接: 灭 */
            else if (StadiaBLE_IsScanning()) want = 1;  /* 扫描: 快闪 */
            else                             want = 0;  /* 空闲: 慢闪 */

            if (want != led_state) {
                led_state = want;
                last_led = now;
        if (want == 3)      platform_led_on();
        else if (want == 2) platform_led_off();
                /* want 0/1: 下面靠翻转维持闪烁 */
            }

            if (led_state <= 1) {
                uint16_t interval = (led_state == 1) ? 100 : 500;
                if ((now - last_led) >= interval) {
                    last_led = now;
                    platform_led_toggle();
                }
            }
        }

        TMOS_SystemProcess();

        /* Discovery 超时: 电池发现卡住时不阻塞后续 GATT 操作 */
        {
            static uint32_t disc_start = 0;
            if (g_stadia_connected && !StadiaBLE_IsScanning()) {
                if (disc_start == 0) disc_start = now;
                else if ((now - disc_start) > 3000) {
                    StadiaBLE_ForceDiscoveryDone();
                    disc_start = 0;
                }
            } else {
                disc_start = 0;
            }
        }

        /* Stadia 输入报告 → SInput USB 发送 */
        if (g_stadia_report_ready && g_stadia_connected) {
            uint32_t buttons;
            uint8_t analog[6];
            if (stadia_parse_report(g_stadia_raw_data, g_stadia_raw_len,
                                     &buttons, analog)) {
                uint8_t report[64];
                sinput_build_report(buttons, analog, g_stadia_battery, report);
                if (!SInputUSB_SendReport(report, 64)) {
                    /* EP3 忙: 保留 ready 标志, 下一轮主循环用最新数据重试.
                     * 如果此日志频繁出现, 说明 USB 是瓶颈; 如果不出现但摇杆仍然卡偏移, 说明是 BLE 丢包 */
                    LOG_DEBUG("HID", "Input report dropped (EP3 busy), will retry");
                } else {
                    g_stadia_report_ready = false;
                }
            } else {
                g_stadia_report_ready = false;
            }
        }

        /* 震动发送: 带背压 + 指数退避
         *   成功 → 清 pending, 重置退避
         *   失败 → 加倍等待: 10ms → 20ms → 40ms → ... → 500ms cap
         *   新数据到达 → 重置退避 (on_hid_output 设 s_rumble_retry_at=0) */
        if (g_rumble_pending && g_stadia_connected && g_hid_output_handle > 0) {
            if (now >= s_rumble_retry_at) {
                uint8_t rumble[4];
                stadia_build_rumble_report(g_rumble_left, g_rumble_right, rumble);
                if (StadiaBLE_SendOutput(rumble, 4)) {
                    LOG_DEBUG("HID", "Rumble sent L=%u R=%u", g_rumble_left, g_rumble_right);
                    g_rumble_pending = false;
                    s_rumble_backoff = 0;
                    s_rumble_timeout_cnt = 0;
                } else {
                    /* 检测连续 bleTimeout — ATT 层可能死锁, 需要断连恢复 */
                    s_rumble_timeout_cnt++;
                    if (s_rumble_timeout_cnt >= RUMBLE_TIMEOUT_LIMIT) {
                        LOG_WARN("HID", "Rumble timeout x%d, resetting BLE", s_rumble_timeout_cnt);
                        StadiaBLE_Disconnect();
                        /* 主循环下一轮会触发重扫 */
                        g_rumble_pending = false;
                        s_rumble_backoff = 0;
                        s_rumble_timeout_cnt = 0;
                    } else {
                        if (s_rumble_backoff == 0)
                            s_rumble_backoff = 10;
                        else {
                            s_rumble_backoff *= 2;
                            if (s_rumble_backoff > 500)
                                s_rumble_backoff = 500;
                        }
                        s_rumble_retry_at = now + s_rumble_backoff;
                        LOG_WARN("HID", "Rumble fail, retry in %ums", s_rumble_backoff);
                    }
                }
            }
        } else {
            s_rumble_backoff = 0;   /* 无 pending 时保持退避清零 */
        }

        /* 周期电池读取 + 日志: 连接后每 60s 读取并打印 */
        {
            static uint32_t s_last_bat_poll = 0;
            if (g_stadia_connected && (now - s_last_bat_poll >= 60000)) {
                s_last_bat_poll = now;
                if (g_stadia_bat_handle > 0)
                    StadiaBLE_ReadBattery();
                /* ReadBattery 会异步更新 g_stadia_battery, 这里仅打印当前值 */
                if (g_stadia_battery <= 100)
                    LOG_INFO("BAT", "Last: %d%%", g_stadia_battery);
                else
                    LOG_INFO("BAT", "Last: unknown");
            }
        }
    }
}
