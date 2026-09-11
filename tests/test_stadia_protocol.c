/********************************************
 * Stick — Stadia 协议纯算法主机单元测试
 *
 * 编译 (host gcc, 不依赖任何硬件):
 *   gcc -Isrc -std=gnu99 -Wall -Wextra
 *       tests/test_stadia_protocol.c src/stadia_protocol.c
 *       -o build/run_tests_protocol.exe
 ********************************************/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "stadia_protocol.h"

/* JP_BUTTON 输出位 (定义于 stadia_protocol.c 内部, 头文件未导出) */
#define JP_BT_DU (1U << 12)
#define JP_BT_DD (1U << 13)
#define JP_BT_DL (1U << 14)
#define JP_BT_DR (1U << 15)

static int s_fail = 0;

static void check(const char *name, bool ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        s_fail = 1;
}

/* 用例 1: 10 字节报告, 无报告 ID */
static void test_no_report_id(void)
{
    uint8_t rpt[10];
    memset(rpt, 0, sizeof(rpt));
    rpt[0] = STADIA_DPAD_UP;  /* dpad = 上 */
    rpt[1] = 0xFF;            /* buttons1 全按 */
    rpt[2] = 0xFF;            /* buttons2 全按 */

    uint32_t buttons = 0;
    uint8_t analog[6] = {0};

    bool ok = stadia_parse_report(rpt, sizeof(rpt), &buttons, analog);

    /* 期望: 面键 0-11 + DU(12) + A1(16) + A2(17) + A3(18),
     * 不含 13/14/15 (dpad 仅 UP 方向), 不含 19 */
    const uint32_t expect = (1U << 0)  | (1U << 1)  | (1U << 2)  | (1U << 3) |
                            (1U << 4)  | (1U << 5)  | (1U << 6)  | (1U << 7) |
                            (1U << 8)  | (1U << 9)  | (1U << 10) | (1U << 11) |
                            (1U << 12) | (1U << 16) | (1U << 17) | (1U << 18);

    check("case1: 10-byte report, no report ID (all buttons + dpad UP)",
          ok && buttons == expect);
}

/* 用例 2: 11 字节报告, 带报告 ID 0x03 */
static void test_report_id_offset(void)
{
    uint8_t rpt[11];
    memset(rpt, 0, sizeof(rpt));
    rpt[0] = 0x03;            /* 报告 ID */
    rpt[1] = STADIA_DPAD_UP;  /* dpad = 上 */
    rpt[2] = 0xFF;            /* buttons1 全按 */
    rpt[3] = 0xFF;            /* buttons2 全按 */

    uint32_t buttons = 0;
    uint8_t analog[6] = {0};

    bool ok = stadia_parse_report(rpt, sizeof(rpt), &buttons, analog);

    const uint32_t expect = (1U << 0)  | (1U << 1)  | (1U << 2)  | (1U << 3) |
                            (1U << 4)  | (1U << 5)  | (1U << 6)  | (1U << 7) |
                            (1U << 8)  | (1U << 9)  | (1U << 10) | (1U << 11) |
                            (1U << 12) | (1U << 16) | (1U << 17) | (1U << 18);

    check("case2: 11-byte report with report ID 0x03 (offset handled)",
          ok && buttons == expect);
}

/* 用例 3: len < 10 → false; NULL data → false */
static void test_short_and_null(void)
{
    uint8_t rpt[10];
    memset(rpt, 0, sizeof(rpt));
    uint32_t buttons = 0;
    uint8_t analog[6] = {0};

    bool short_ok = !stadia_parse_report(rpt, 9, &buttons, analog);
    bool null_ok  = !stadia_parse_report(NULL, 10, &buttons, analog);

    check("case3: len < 10 returns false; NULL buffer returns false",
          short_ok && null_ok);
}

/* 用例 4: D-pad 全部 8 个方向 */
static void test_dpad_all_directions(void)
{
    static const struct {
        uint8_t dpad;
        uint32_t expect;
    } cases[] = {
        { STADIA_DPAD_UP,         JP_BT_DU },
        { STADIA_DPAD_UP_RIGHT,   JP_BT_DU | JP_BT_DR },
        { STADIA_DPAD_RIGHT,      JP_BT_DR },
        { STADIA_DPAD_DOWN_RIGHT, JP_BT_DR | JP_BT_DD },
        { STADIA_DPAD_DOWN,       JP_BT_DD },
        { STADIA_DPAD_DOWN_LEFT,  JP_BT_DD | JP_BT_DL },
        { STADIA_DPAD_LEFT,       JP_BT_DL },
        { STADIA_DPAD_UP_LEFT,    JP_BT_DL | JP_BT_DU },
    };

    bool all = true;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t rpt[10];
        memset(rpt, 0, sizeof(rpt));
        rpt[0] = cases[i].dpad;

        uint32_t buttons = 0;
        uint8_t analog[6] = {0};
        if (!stadia_parse_report(rpt, sizeof(rpt), &buttons, analog) ||
            buttons != cases[i].expect)
            all = false;
    }

    check("case4: D-pad all 8 directions (DU/DD/DL/DR combos)", all);
}

/* 用例 5: 模拟值透传 rpt[3..8] → analog_out[0..5] */
static void test_analog_passthrough(void)
{
    uint8_t rpt[10];
    memset(rpt, 0, sizeof(rpt));
    /* 使用互不相同的值, 按 [LX, LY, RX, RY, L2, R2] 填入 */
    rpt[3] = 0x11;
    rpt[4] = 0x22;
    rpt[5] = 0x33;
    rpt[6] = 0x44;
    rpt[7] = 0x55;
    rpt[8] = 0x66;

    uint32_t buttons = 0;
    uint8_t analog[6] = {0};
    bool ok = stadia_parse_report(rpt, sizeof(rpt), &buttons, analog);

    check("case5: analog passthrough rpt[3..8] -> analog_out[0..5]",
          ok &&
          analog[0] == 0x11 && analog[1] == 0x22 &&
          analog[2] == 0x33 && analog[3] == 0x44 &&
          analog[4] == 0x55 && analog[5] == 0x66);
}

/* 用例 6: 任一必需参数为 NULL → false */
static void test_null_arguments(void)
{
    uint8_t rpt[10];
    memset(rpt, 0, sizeof(rpt));
    uint32_t buttons = 0;
    uint8_t analog[6] = {0};

    bool d_ok  = !stadia_parse_report(NULL, sizeof(rpt), &buttons, analog);
    bool b_ok  = !stadia_parse_report(rpt, sizeof(rpt), NULL, analog);
    bool a_ok  = !stadia_parse_report(rpt, sizeof(rpt), &buttons, NULL);

    check("case6: NULL in any required argument returns false",
          d_ok && b_ok && a_ok);
}

/* 用例 7: 震动报告构建 */
static void test_rumble(void)
{
    uint8_t buf[4];

    stadia_build_rumble_report(255, 0, buf);
    bool max_l = (buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0 && buf[3] == 0);

    stadia_build_rumble_report(0, 255, buf);
    bool max_r = (buf[0] == 0 && buf[1] == 0 && buf[2] == 0xFF && buf[3] == 0xFF);

    stadia_build_rumble_report(1, 0, buf);
    bool min_l = (buf[0] == 0x01 && buf[1] == 0x01 && buf[2] == 0 && buf[3] == 0);

    check("case7: rumble build (255/0, 0/255, 1/0 -> 16-bit LE x257)",
          max_l && max_r && min_l);
}

int main(void)
{
    test_no_report_id();
    test_report_id_offset();
    test_short_and_null();
    test_dpad_all_directions();
    test_analog_passthrough();
    test_null_arguments();
    test_rumble();

    printf("RESULT: %s\n", s_fail ? "FAIL" : "PASS");
    return s_fail ? 1 : 0;
}
