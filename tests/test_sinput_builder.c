/********************************************
 * Stick — SInput 构建纯算法主机单元测试
 *
 * 编译 (host gcc, 不依赖任何硬件):
 *   gcc -Isrc -std=gnu99 -Wall -Wextra
 *       tests/test_sinput_builder.c src/sinput_builder.c
 *       -o build/run_tests_builder.exe
 *
 * 注意: 本文件只链接 sinput_builder.c, 不含震动构建用例
 * (stadia_build_rumble_report 在 stadia_protocol.c, 会链接失败)。
 ********************************************/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "sinput_builder.h"

static int s_fail = 0;

static void check(const char *name, bool ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        s_fail = 1;
}

/* 从 4 字节 LE 按键位图读回 32 位值 */
static uint32_t read_buttons(const sinput_report_t *rpt)
{
    return (uint32_t)rpt->buttons[0] |
           ((uint32_t)rpt->buttons[1] << 8) |
           ((uint32_t)rpt->buttons[2] << 16) |
           ((uint32_t)rpt->buttons[3] << 24);
}

/* 用例 1: 基线报告 (摇杆居中, 扳机为 0) */
static void test_build_report_baseline(void)
{
    uint8_t out[64];
    const uint8_t analog[6] = {128, 128, 128, 128, 0, 0};

    sinput_build_report(0, analog, 100, out);

    const sinput_report_t *rpt = (const sinput_report_t *)out;
    /* 扳机转换是 (v*32767)/255: 输入必须是 0 才能断言 lt=rt=0 */
    check("case1: report_id/plug/charge/axes (centered sticks, zero triggers)",
          rpt->report_id == SINPUT_REPORT_ID_INPUT &&
          rpt->plug_status == 4 &&
          rpt->charge_level == 100 &&
          rpt->lx == 0 && rpt->ly == 0 &&
          rpt->rx == 0 && rpt->ry == 0 &&
          rpt->lt == 0 && rpt->rt == 0 &&
          read_buttons(rpt) == 0);
}

/* 用例 2: 18 个按键位逐一映射 */
static void test_button_mapping(void)
{
    static const struct {
        uint32_t jp;
        uint32_t mask;
    } map[] = {
        { 1U << 0,  SINPUT_MASK_SOUTH   },
        { 1U << 1,  SINPUT_MASK_EAST    },
        { 1U << 2,  SINPUT_MASK_WEST    },
        { 1U << 3,  SINPUT_MASK_NORTH   },
        { 1U << 4,  SINPUT_MASK_L1      },
        { 1U << 5,  SINPUT_MASK_R1      },
        { 1U << 6,  SINPUT_MASK_L2      },
        { 1U << 7,  SINPUT_MASK_R2      },
        { 1U << 8,  SINPUT_MASK_BACK    },
        { 1U << 9,  SINPUT_MASK_START   },
        { 1U << 10, SINPUT_MASK_L3      },
        { 1U << 11, SINPUT_MASK_R3      },
        { 1U << 12, SINPUT_MASK_DU      },
        { 1U << 13, SINPUT_MASK_DD      },
        { 1U << 14, SINPUT_MASK_DL      },
        { 1U << 15, SINPUT_MASK_DR      },
        { 1U << 16, SINPUT_MASK_GUIDE   },
        { 1U << 17, SINPUT_MASK_CAPTURE },
    };

    bool all = true;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        uint8_t out[64];
        const uint8_t analog[6] = {128, 128, 128, 128, 0, 0};

        sinput_build_report(map[i].jp, analog, 100, out);
        const sinput_report_t *rpt = (const sinput_report_t *)out;

        if (read_buttons(rpt) != map[i].mask)
            all = false;
    }

    check("case2: all 18 button bits mapped individually", all);
}

/* 用例 3: 轴边界 (最大值) */
static void test_axis_bounds(void)
{
    uint8_t out[64];
    const uint8_t analog[6] = {255, 128, 128, 128, 255, 0};

    sinput_build_report(0, analog, 100, out);

    const sinput_report_t *rpt = (const sinput_report_t *)out;
    /* analog[0]=255 → lx=(255-128)*256=32512;
     * analog[4]=255 → lt=(255*32767)/255=32767 */
    check("case3: axis bounds (lx=32512, lt=32767)",
          rpt->lx == 32512 && rpt->lt == 32767);
}

/* 用例 4: 63 字节功能响应 */
static void test_feature_response(void)
{
    const uint8_t serial[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    uint8_t out[63];

    sinput_build_feature_response(serial, out);

    bool tail_zero = true;
    for (int i = 9; i <= 62; i++) {
        if (i >= 13 && i <= 16)
            continue;               /* 按键掩码区 (单独断言) */
        if (i >= 19 && i <= 24)
            continue;               /* 序列号区 */
        if (out[i] != 0)
            tail_zero = false;
    }

    check("case4: feature response (version/caps/type/face/poll/masks/serial)",
          out[0] == 0x02 &&                       /* 命令回显 */
          out[1] == 0x00 && out[2] == 0x01 &&     /* 版本 0x0100 LE */
          out[3] == 0xF3 &&                       /* 能力标志 1 */
          out[4] == 0x02 &&                       /* 能力标志 2 */
          out[5] == 1 &&                          /* 手柄类型 */
          out[6] == (1 << 5) &&                   /* 面键风格 1<<5 */
          out[7] == 0xE8 && out[8] == 0x03 &&     /* 轮询 1000Hz 0x03E8 */
          out[13] == 0xFF && out[14] == 0xFF &&   /* 按键掩码 */
          out[15] == 0x0F && out[16] == 0x00 &&
          out[19] == serial[0] && out[20] == serial[1] &&
          out[21] == serial[2] && out[22] == serial[3] &&
          out[23] == serial[4] && out[24] == serial[5] &&
          tail_zero);
}

/* 用例 5: 震动命令解析 */
static void test_haptic_parse(void)
{
    uint8_t data[7] = {0x03, 0x01, 2, 0x64, 0, 0x32, 0};
    uint8_t left = 0, right = 0;

    /* 有效命令: report_id=0x03, cmd=0x01, type=2, L=0x64, R=0x32 */
    bool valid = sinput_parse_haptic(data, 7, &left, &right) &&
                 left == 0x64 && right == 0x32;
    check("case5: haptic parse valid (L=0x64, R=0x32)", valid);

    data[1] = 0x02;   /* 错误命令字节 (0x02 = 功能查询) */
    check("case5: wrong command byte -> false",
          !sinput_parse_haptic(data, 7, &left, &right));

    data[1] = 0x01;   /* 恢复 */
    check("case5: len < 7 -> false",
          !sinput_parse_haptic(data, 6, &left, &right));

    data[0] = 0x01;   /* 错误报告 ID */
    check("case5: wrong report_id -> false",
          !sinput_parse_haptic(data, 7, &left, &right));

    check("case5: NULL args -> false",
          !sinput_parse_haptic(NULL, 7, &left, &right) &&
          !sinput_parse_haptic(data, 7, NULL, &right) &&
          !sinput_parse_haptic(data, 7, &left, NULL));
}

int main(void)
{
    test_build_report_baseline();
    test_button_mapping();
    test_axis_bounds();
    test_feature_response();
    test_haptic_parse();

    printf("RESULT: %s\n", s_fail ? "FAIL" : "PASS");
    return s_fail ? 1 : 0;
}
