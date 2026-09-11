/********************************************
 * Stick — CH592F Stadia USB Dongle
 * BLE Central — 扫描 Stadia 手柄、连接、HID 通知
 *
 * 参考:
 *   sdk/EVT/EXAM/BLE/Central/APP/central.c  — BLE Central 完整实现 (主参考)
 *   sdk/EVT/EXAM/BLE/Central/APP/central_main.c — 初始化序列
 ********************************************/

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "stadia_ble.h"
#include "CONFIG.h"
#include "log.h"

/* ---- 服务 UUID ---- */
#define HID_SERVICE_UUID        0x1812
#define HID_INPUT_REPORT_UUID   0x2A4D
#define HID_OUTPUT_REPORT_UUID  0x2A4D  /* 同一 UUID, 用特征属性区分 */
#define CCCD_UUID               0x2902
#define BATTERY_SERVICE_UUID    0x180F
#define BATTERY_LEVEL_UUID      0x2A19

/* ---- 扫描/连接参数 (与 SDK central.c 一致) ---- */
#define DEFAULT_SCAN_RES_MAX    10
#define DEFAULT_SCAN_DURATION   2400    /* 2400 * 0.625ms = 1.5s */
#define DEFAULT_CONN_INTERVAL   20
#define DEFAULT_CONN_TIMEOUT    100
#define SVC_DISCOVERY_DELAY     1600    /* 1600 * 0.625ms = 1s */

/* ---- 应用状态 (参考 central.c) ---- */
enum {
    BLE_STATE_IDLE,
    BLE_STATE_SCANNING,
    BLE_STATE_CONNECTING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTING
};

/* ---- 服务发现状态 ---- */
enum {
    DISC_IDLE,
    DISC_SVC,            /* 发现 HID 服务 */
    DISC_CHAR_ALL,       /* 发现 HID 服务内所有特征 (按属性区分输入/输出) */
    DISC_CCCD,           /* 使能 HID 通知 */
    DISC_BAT_SVC,        /* 发现电池服务 */
    DISC_BAT_CHAR,       /* 发现电池电量特征 */
    DISC_BAT_CCCD        /* 使能电池通知 */
};

/* ---- 全局状态 ---- */
bool     g_stadia_connected = false;
volatile bool g_stadia_report_ready = false;
uint8_t  g_stadia_raw_data[16] = {0};
uint16_t g_stadia_raw_len = 0;
uint16_t g_conn_handle = 0;
uint16_t g_hid_input_handle = 0;
uint16_t g_hid_output_handle = 0;
uint8_t  g_hid_output_props = 0;
uint16_t g_stadia_bat_handle = 0;   /* 电池特征值句柄, 用于接收通知 */
uint8_t  g_stadia_battery = 0xFF;
bool     g_stadia_bat_read_pending = false;
bool     g_ble_ready = false;           /* GAP_DEVICE_INIT_DONE 完成后才允许操作 */

/* ---- 模块内部状态 ---- */
static uint8_t  s_task_id;
static uint8_t  s_state = BLE_STATE_IDLE;
static uint8_t  s_disc_state = DISC_IDLE;
static uint8_t  s_scan_res;                      /* 扫描结果数 */
/* 扫描结果去重表: 保留用于设备计数与去重 (省 ~100B 收益小于回归风险, 刻意保留) */
static gapDevRec_t s_dev_list[DEFAULT_SCAN_RES_MAX];
static uint16_t s_svc_start, s_svc_end;
static uint16_t s_cccd_handle;
static uint16_t s_bat_cccd_handle;
static bool     s_procedure_in_progress = FALSE; /* GATT 操作进行中 (参考 SDK central.c) */

/* 找到的 Stadia 设备 */
static bool     s_stadia_found = false;
static uint8_t  s_stadia_addr[B_ADDR_LEN];
static uint8_t  s_stadia_addr_type;

/* 系统毫秒时间戳 (由 main.c 的 update_tick 刷新) */
extern volatile uint32_t g_sys_tick_ms;

/* ---- TMOS 事件 ID ---- */
#define START_DEVICE_EVT        0x0001
#define START_SVC_DISCOVERY_EVT 0x0002

/* ---- BLE 通知连续性监控 ---- */
static uint32_t s_last_noti_ms = 0;
#define NOTI_GAP_WARN_MS  100    /* 通知间隔超过此值(ms)打印警告 */

/* ---- 函数声明 ---- */
static uint16_t stadia_ProcessEvent(uint8_t task_id, uint16_t events);
static void stadia_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void stadia_EventCB(gapRoleEvent_t *pEvent);
static void stadia_GATTMsgCB(gattMsgEvent_t *pMsg);
static void stadia_StartDiscovery(void);
static void stadia_GATTDiscoveryEvent(gattMsgEvent_t *pMsg);
static void stadia_AddDevice(uint8_t *pAddr, uint8_t addrType);

/* ---- GAP Role 回调 (注册到 GAPRole_CentralStartDevice) ---- */
static void stadia_PasscodeCB(uint8_t *deviceAddr, uint16_t connectionHandle,
                               uint8_t uiInputs, uint8_t uiOutputs)
{
    /* Stadia 不需要配对码 */
    GAPBondMgr_PasscodeRsp(connectionHandle, SUCCESS, 0);
}

static void stadia_PairStateCB(uint16_t connHandle, uint8_t state, uint8_t status)
{
    if (state == GAPBOND_PAIRING_STATE_COMPLETE) {
        LOG_INFO("BLE", "Pairing %s", status == SUCCESS ? "success" : "fail");
    }
}

static gapCentralRoleCB_t s_central_role_cb = {
    NULL,                /* RSSI 回调 (不使用) */
    stadia_EventCB,      /* 事件回调 */
    NULL                 /* MTU 变更回调 (不使用) */
};

static gapBondCBs_t s_central_bond_cb = {
    stadia_PasscodeCB,   /* 配对码回调 */
    stadia_PairStateCB,  /* 配对状态回调 */
    NULL                 /* OOB 回调 (不使用) */
};

/* ===================================================================
 * 初始化 (参考 central.c Central_Init)
 * =================================================================== */
void StadiaBLE_Init(void)
{
    s_task_id = TMOS_ProcessEventRegister(stadia_ProcessEvent);

    /* GAP 参数 (与 SDK central.c 一致) */
    GAP_SetParamValue(TGAP_DISC_SCAN, DEFAULT_SCAN_DURATION);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MIN, DEFAULT_CONN_INTERVAL);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MAX, DEFAULT_CONN_INTERVAL);
    GAP_SetParamValue(TGAP_CONN_EST_SUPERV_TIMEOUT, DEFAULT_CONN_TIMEOUT);

    /* GATT Client 初始化 */
    GATT_InitClient();
    GATT_RegisterForInd(s_task_id);

    /* Bond Manager 配置 (无需配对绑定) */
    {
        uint32_t passkey = 0;
        uint8_t  pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
        uint8_t  mitm = FALSE;
        uint8_t  ioCap = GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT;
        uint8_t  bonding = FALSE;
        GAPBondMgr_SetParameter(GAPBOND_CENT_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_CENT_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_CENT_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_CENT_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_CENT_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    /* 延迟启动: TMOS 就绪后调用 GAPRole_CentralStartDevice */
    tmos_set_event(s_task_id, START_DEVICE_EVT);
    LOG_INFO("BLE", "Init done");
}

/* ---- 扫描控制 ---- */
void StadiaBLE_StartScan(void)
{
    if (s_state != BLE_STATE_IDLE) return;
    s_state = BLE_STATE_SCANNING;
    s_scan_res = 0;
    s_stadia_found = false;
    GAPRole_CentralStartDiscovery(DEVDISC_MODE_ALL, TRUE, FALSE);
    LOG_INFO("BLE", "Scanning for Stadia...");
}

void StadiaBLE_StopScan(void)
{
    GAPRole_CentralCancelDiscovery();
    s_state = BLE_STATE_IDLE;
    LOG_INFO("BLE", "Scan cancelled");
}

bool StadiaBLE_IsScanning(void)
{
    return (s_state == BLE_STATE_SCANNING);
}

void StadiaBLE_Disconnect(void)
{
    if (s_state == BLE_STATE_CONNECTED) {
        s_state = BLE_STATE_DISCONNECTING;
        GAPRole_TerminateLink(g_conn_handle);
    }
}

bool StadiaBLE_SendOutput(uint8_t *data, uint16_t len)
{
    if (!g_stadia_connected || g_hid_output_handle == 0) return false;

    /*
     * 始终使用 Write Command (GATT_WriteNoRsp), 无视特征属性标志。
     *
     * 原因: Stadia 手柄输出报告只支持 Write Request (无 WRITE_NO_RSP 标志),
     *       但 Write Request 要求回执。回执一旦丢失, WCH 栈 ATT 过程标志
     *       永久阻塞, 所有后续 GATT 操作返回 bleTimeout(23), 振动永久失效。
     *
     * Write Command 无回执、不阻塞 ATT, 是最安全的选择。实测 Stadia 手柄
     * 接受 Write Command 写入输出报告特征, 即使该特征未声明 WRITE_NO_RSP。
     */
    attWriteReq_t req;
    req.cmd    = TRUE;
    req.sig    = FALSE;
    req.handle = g_hid_output_handle;
    req.len    = len;
    req.pValue = GATT_bm_alloc(g_conn_handle, ATT_WRITE_CMD, len, NULL, 0);
    if (req.pValue == NULL) {
        LOG_DEBUG("BLE", "WrCmd alloc fail");
        return false;
    }
    memcpy(req.pValue, data, len);
    bStatus_t status = GATT_WriteNoRsp(g_conn_handle, &req);
    if (status == SUCCESS) {
        LOG_DEBUG("BLE", "WrCmd OK");
        return true;
    }
    LOG_DEBUG("BLE", "WrCmd fail: status=%d", status);
    GATT_bm_free((gattMsg_t *)&req, ATT_WRITE_CMD);
    return false;
}

/* ===================================================================
 * 电池电量读取 (主循环周期调用)
 * =================================================================== */
void StadiaBLE_ReadBattery(void)
{
    if (g_stadia_bat_handle > 0 && g_stadia_connected && !g_stadia_bat_read_pending) {
        attReadReq_t rd;
        memset(&rd, 0, sizeof(rd));
        rd.handle = g_stadia_bat_handle;
        if (GATT_ReadCharValue(g_conn_handle, &rd, s_task_id) == SUCCESS)
            g_stadia_bat_read_pending = true;
    }
}

/* ===================================================================
 * TMOS 事件处理 (参考 central.c Central_ProcessEvent)
 * =================================================================== */
static uint16_t stadia_ProcessEvent(uint8_t task_id, uint16_t events)
{
    if (events & SYS_EVENT_MSG) {
        uint8_t *pMsg;
        if ((pMsg = tmos_msg_receive(task_id)) != NULL) {
            stadia_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            tmos_msg_deallocate(pMsg);
        }
        return events ^ SYS_EVENT_MSG;
    }

    if (events & START_DEVICE_EVT) {
        /* 启动 BLE 设备: 注册角色回调, 触发 GAP_DEVICE_INIT_DONE_EVENT
         * 注意: GAPRole_CentralStartDevice 只调用一次, 不重复调用 */
        GAPRole_CentralStartDevice(s_task_id, &s_central_bond_cb, &s_central_role_cb);
        return events ^ START_DEVICE_EVT;
    }

    if (events & START_SVC_DISCOVERY_EVT) {
        stadia_StartDiscovery();
        return events ^ START_SVC_DISCOVERY_EVT;
    }

    return 0;
}

static void stadia_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    if (pMsg->event == GATT_MSG_EVENT)
        stadia_GATTMsgCB((gattMsgEvent_t *)pMsg);
}

/* ===================================================================
 * GAP 事件回调 (参考 central.c centralEventCB)
 * =================================================================== */
static void stadia_EventCB(gapRoleEvent_t *pEvent)
{
    /* 调试: 打印所有 GAP 事件 */
    LOG_DEBUG("BLE", "Event opcode=0x%02x", pEvent->gap.opcode);

    switch (pEvent->gap.opcode) {

    case GAP_DEVICE_INIT_DONE_EVENT:
    {
        /* 配置静态地址 (SDK 标准做法) */
        uint8_t ownAddr[B_ADDR_LEN];
        GAPRole_GetParameter(GAPROLE_BD_ADDR, ownAddr);
        GAP_ConfigDeviceAddr(ADDRTYPE_STATIC, ownAddr);

        g_ble_ready = true;  /* 允许用户按 BOOT 操作 */
        LOG_INFO("BLE", "Init done, press BOOT to scan");
        s_state = BLE_STATE_IDLE;
        break;
    }

    case GAP_DEVICE_INFO_EVENT:
    {
        /* 扫描到设备: 检查名称是否为 "Stadia"
         * 参考: 开发者将 centralAddDeviceInfo 改为名称过滤 */
        if (s_stadia_found) break;  /* 已找到, 忽略后续广播 */
        LOG_DEBUG("BLE", "Dev found, dataLen=%u", pEvent->deviceInfo.dataLen);

        uint8_t *p = pEvent->deviceInfo.pEvtData;
        uint8_t len = pEvent->deviceInfo.dataLen;
        for (int i = 0; i + 1 < (int)len; ) {
            uint8_t field_len = p[i];
            if (field_len == 0) break;
            uint8_t field_type = p[i + 1];
            if (field_type == 0x08 || field_type == 0x09) {
                uint8_t name_len = field_len - 1;
                const char *name = (const char *)&p[i + 2];
                if (name_len >= 6 && memcmp(name, "Stadia", 6) == 0) {
                    LOG_INFO("BLE", "Found Stadia!");
                    s_stadia_found = true;
                    memcpy(s_stadia_addr, pEvent->deviceInfo.addr, B_ADDR_LEN);
                    s_stadia_addr_type = pEvent->deviceInfo.addrType;
                    /* 立即取消扫描, 触发 GAP_DEVICE_DISCOVERY_EVENT */
                    GAPRole_CentralCancelDiscovery();
                    return;
                }
            }
            i += field_len + 1;
        }
        /* 非 Stadia 设备也记录到列表 */
        stadia_AddDevice(pEvent->deviceInfo.addr, pEvent->deviceInfo.addrType);
        break;
    }

    case GAP_DEVICE_DISCOVERY_EVENT:
    {
        if (s_stadia_found) {
            /* 已找到 Stadia, 发起连接 */
            LOG_INFO("BLE", "Connecting...");
            s_state = BLE_STATE_CONNECTING;
            GAPRole_CentralEstablishLink(FALSE, FALSE,
                                          s_stadia_addr_type,
                                          s_stadia_addr);
        } else if (s_state == BLE_STATE_SCANNING) {
            /* 扫描超时未找到 Stadia → 继续扫描 (参考 SDK 重扫逻辑) */
            LOG_DEBUG("BLE", "Stadia not found (%u devices seen), rescanning...", s_scan_res);
            s_scan_res = 0;
            GAPRole_CentralStartDiscovery(DEVDISC_MODE_ALL, TRUE, FALSE);
        }
        break;
    }

    case GAP_LINK_ESTABLISHED_EVENT:
    {
        if (pEvent->gap.hdr.status == SUCCESS) {
            /* 连接成功 */
            g_conn_handle = pEvent->linkCmpl.connectionHandle;
            g_stadia_connected = true;
            s_state = BLE_STATE_CONNECTED;
            s_procedure_in_progress = TRUE;
            LOG_INFO("BLE", "Connected! handle=%d", g_conn_handle);

            /* 发起 MTU 交换 (参考 SDK central.c) */
            attExchangeMTUReq_t mtu_req = {
                .clientRxMTU = BLE_BUFF_MAX_LEN - 4,
            };
            GATT_ExchangeMTU(g_conn_handle, &mtu_req, s_task_id);

            /* 延迟启动服务发现 (给 MTU 交换留时间) */
            tmos_start_task(s_task_id, START_SVC_DISCOVERY_EVT, SVC_DISCOVERY_DELAY);

            StadiaBLE_OnConnected(g_conn_handle);
        } else {
            /* 连接失败 → 回到 IDLE */
            LOG_WARN("BLE", "Connect fail: 0x%x", pEvent->gap.hdr.status);
            s_state = BLE_STATE_IDLE;
            s_stadia_found = false;
            /* SDK central.c 在连接失败后自动重启扫描 */
            s_scan_res = 0;
            GAPRole_CentralStartDiscovery(DEVDISC_MODE_ALL, TRUE, FALSE);
        }
        break;
    }

    case GAP_LINK_TERMINATED_EVENT:
    {
        LOG_WARN("BLE", "Disconnected, reason=0x%x",
                 pEvent->linkTerminate.reason);
        /* 清空状态 */
        s_state = BLE_STATE_IDLE;
        s_disc_state = DISC_IDLE;
        s_stadia_found = false;
        g_stadia_connected = false;
        g_conn_handle = 0;
        g_hid_input_handle = 0;
        g_hid_output_handle = 0;
        g_hid_output_props = 0;
        g_stadia_battery = 0xFF;
        g_stadia_bat_handle = 0;
        s_procedure_in_progress = FALSE;
        g_stadia_report_ready = false;   /* 丢弃断链前的残留输入报告 */
        g_stadia_raw_len = 0;
        StadiaBLE_OnDisconnected(pEvent->linkTerminate.connectionHandle);
        break;
    }

    default:
        break;
    }
}

/* ===================================================================
 * GATT 消息处理 (参考 central.c centralProcessGATTMsg + centralGATTDiscoveryEvent)
 * =================================================================== */
static void stadia_GATTMsgCB(gattMsgEvent_t *pMsg)
{
    /* 连接已断开时忽略所有 GATT 消息 (参考 SDK) */
    if (s_state != BLE_STATE_CONNECTED && s_disc_state == DISC_IDLE) {
        GATT_bm_free(&pMsg->msg, pMsg->method);
        return;
    }

    /* ---- Debug: 打印所有 GATT 消息类型 ---- */
    LOG_DEBUG("GATT", "method=0x%02x state=%d disc=%d",
             pMsg->method, s_state, s_disc_state);

    /* ---- MTU 交换处理 (参考 SDK) ---- */
    if ((pMsg->method == ATT_EXCHANGE_MTU_RSP) ||
        ((pMsg->method == ATT_ERROR_RSP) &&
         (pMsg->msg.errorRsp.reqOpcode == ATT_EXCHANGE_MTU_REQ))) {
        if (pMsg->method == ATT_ERROR_RSP) {
            LOG_WARN("BLE", "MTU exchange error: 0x%x",
                     pMsg->msg.errorRsp.errCode);
        }
        s_procedure_in_progress = FALSE;
    }

    if (pMsg->method == ATT_MTU_UPDATED_EVENT) {
        LOG_INFO("BLE", "MTU: %d", pMsg->msg.mtuEvt.MTU);
    }

    /* ---- 电池读回执 (GATT_ReadCharValue 的响应) ---- */
    if (pMsg->method == ATT_READ_RSP && g_stadia_bat_read_pending) {
        g_stadia_bat_read_pending = false;
        LOG_DEBUG("BAT", "ReadRsp len=%d", pMsg->msg.readRsp.len);
        if (g_stadia_bat_handle > 0 &&
            pMsg->msg.readRsp.len >= 1) {
            uint8_t val = pMsg->msg.readRsp.pValue[0];
            if (val <= 100) {
                g_stadia_battery = val;
                LOG_INFO("BAT", "Read: %d%%", val);
            }
        }
        /* 不 return, 让默认路径释放 */
    }

    /* ---- 写响应/错误日志 (不影响 GATT 消息流, 由默认路径释放) ---- */
    if (s_disc_state == DISC_IDLE) {
        if (pMsg->method == ATT_WRITE_RSP) {
            LOG_INFO("BLE", "Write OK (ack)");
        }
        if (pMsg->method == ATT_ERROR_RSP &&
            pMsg->msg.errorRsp.reqOpcode == ATT_WRITE_REQ) {
            LOG_WARN("BLE", "Write rejected: err=0x%02x opcode=0x%02x",
                     pMsg->msg.errorRsp.errCode,
                     pMsg->msg.errorRsp.reqOpcode);
        }
    }

    /* ---- HID 通知 (输入报告) ---- */
    if (pMsg->method == ATT_HANDLE_VALUE_NOTI) {
        uint16_t h = pMsg->msg.handleValueNoti.handle;
        uint16_t len = pMsg->msg.handleValueNoti.len;

        /* BLE 通知连续性监控: 检测异常间隙 (可能丢包或链路质量问题) */
        {
            uint32_t now = g_sys_tick_ms;
            uint32_t gap = s_last_noti_ms ? (now - s_last_noti_ms) : 0;
            if (gap > NOTI_GAP_WARN_MS) {
                LOG_DEBUG("BLE", "Noti gap %ums (last=%lu now=%lu)", gap, s_last_noti_ms, now);
            }
            s_last_noti_ms = now;
        }

        if (h == g_hid_input_handle) {
            if (len > sizeof(g_stadia_raw_data))
                len = sizeof(g_stadia_raw_data);
            memcpy(g_stadia_raw_data, pMsg->msg.handleValueNoti.pValue, len);
            g_stadia_raw_len = len;
            g_stadia_report_ready = true;
            LOG_DEBUG("HID", "Rx %d bytes (buttons=%02X%02X%02X%02X)",
                     len,
                     g_stadia_raw_data[1], g_stadia_raw_data[2],
                     g_stadia_raw_data[3], g_stadia_raw_data[4]);
        }
        else if (g_stadia_bat_handle > 0 && h == g_stadia_bat_handle) {
            /* 电池通知: 来自电池特征值句柄 */
            if (len >= 1) {
                g_stadia_battery = pMsg->msg.handleValueNoti.pValue[0];
                LOG_INFO("BAT", "Battery: %d%%", g_stadia_battery);
            }
        }
        /* 通知处理完后不继续走 discovery 分支 */
        GATT_bm_free(&pMsg->msg, pMsg->method);
        return;
    }

    /* ---- 发现阶段 (GATT discovery responses) ---- */
    if (s_disc_state != DISC_IDLE) {
        stadia_GATTDiscoveryEvent(pMsg);
    }

    GATT_bm_free(&pMsg->msg, pMsg->method);
}

/* ===================================================================
 * 服务发现 (参考 central.c centralStartDiscovery + centralGATTDiscoveryEvent)
 * =================================================================== */
static void stadia_StartDiscovery(void)
{
    s_disc_state = DISC_SVC;
    s_svc_start = s_svc_end = 0;
    s_cccd_handle = 0;
    s_bat_cccd_handle = 0;

    uint8_t uuid[ATT_BT_UUID_SIZE] = {
        LO_UINT16(HID_SERVICE_UUID),
        HI_UINT16(HID_SERVICE_UUID)
    };
    GATT_DiscPrimaryServiceByUUID(g_conn_handle, uuid, ATT_BT_UUID_SIZE, s_task_id);
    LOG_INFO("BLE", "Discovering HID service...");
}

static void stadia_GATTDiscoveryEvent(gattMsgEvent_t *pMsg)
{
    if (s_disc_state == DISC_SVC) {
        /* HID 服务发现完成 */
        if (pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP &&
            pMsg->msg.findByTypeValueRsp.numInfo > 0) {
            s_svc_start = ATT_ATTR_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
            s_svc_end   = ATT_GRP_END_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
            LOG_INFO("BLE", "HID svc: 0x%X-0x%X", s_svc_start, s_svc_end);
        }
        if ((pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP &&
             pMsg->hdr.status == bleProcedureComplete) ||
            (pMsg->method == ATT_ERROR_RSP)) {
            if (s_svc_start != 0) {
                /* 发现 HID 服务内所有特征 (用 DiscAllChars 替代两次 ReadUsingCharUUID,
                 * 避免 UUID 0x2A4D 重复匹配同一个特征) */
                s_disc_state = DISC_CHAR_ALL;
                g_hid_input_handle = 0;
                g_hid_output_handle = 0;
                GATT_DiscAllChars(g_conn_handle, s_svc_start, s_svc_end, s_task_id);
                LOG_INFO("BLE", "Discovering all chars in HID service...");
            } else {
                LOG_WARN("BLE", "HID service not found");
                s_disc_state = DISC_IDLE;
            }
        }
    }
    else if (s_disc_state == DISC_CHAR_ALL) {
        /* GATT_DiscAllChars 返回 ATT_READ_BY_TYPE_RSP (method=0x09)
         * 每对数据格式: declHandle(2) + properties(1) + valueHandle(2) + UUID(2/16)
         * len=7 for 16-bit UUID */
        if (pMsg->method == ATT_READ_BY_TYPE_RSP &&
            pMsg->msg.readByTypeRsp.numPairs > 0) {
            uint8_t *pData = pMsg->msg.readByTypeRsp.pDataList;
            for (int i = 0; i < pMsg->msg.readByTypeRsp.numPairs; i++) {
                uint8_t pairLen = pMsg->msg.readByTypeRsp.len;
                uint8_t  properties = pData[2];
                uint16_t valueHandle = BUILD_UINT16(pData[3], pData[4]);
                uint16_t uuid = BUILD_UINT16(pData[5], pData[6]);

                if (uuid == HID_INPUT_REPORT_UUID) {
                    if (properties & 0x10) {  /* NOTIFY = 输入报告 */
                        g_hid_input_handle = valueHandle;
                        LOG_INFO("BLE", "Input report: 0x%X (props=0x%02X)",
                                 valueHandle, properties);
                    }
                    if (properties & 0x0C) {  /* WRITE(0x08) | WRITE_NO_RSP(0x04) */
                        g_hid_output_handle = valueHandle;
                        g_hid_output_props = properties;
                        LOG_INFO("BLE", "Output report: 0x%X (props=0x%02X)%s",
                                 valueHandle, properties,
                                 (properties & 0x04) ? " [WrCmd]" : " [WrReq]");
                    }
                }
                pData += pairLen;
            }
        }
        if ((pMsg->method == ATT_READ_BY_TYPE_RSP &&
             pMsg->hdr.status == bleProcedureComplete) ||
            (pMsg->method == ATT_ERROR_RSP)) {
            if (g_hid_input_handle == 0) {
                LOG_WARN("BLE", "No input report found!");
                s_disc_state = DISC_IDLE;
            } else {
                /* 没找到输出句柄时, 尝试用输入句柄写(部分手柄支持) */
                if (g_hid_output_handle == 0) {
                    LOG_WARN("BLE", "No output report found");
                }
                LOG_INFO("BLE", "HID: in=0x%X out=0x%X",
                         g_hid_input_handle, g_hid_output_handle);

                s_cccd_handle = g_hid_input_handle + 1;
                s_disc_state = DISC_CCCD;

                attWriteReq_t wr;
                memset(&wr, 0, sizeof(wr));
                wr.cmd    = FALSE;
                wr.sig    = FALSE;
                wr.handle = s_cccd_handle;
                wr.len    = 2;
                wr.pValue = GATT_bm_alloc(g_conn_handle, ATT_WRITE_REQ, 2, NULL, 0);
                if (wr.pValue) {
                    wr.pValue[0] = 0x01;
                    wr.pValue[1] = 0x00;
                    GATT_WriteCharValue(g_conn_handle, &wr, s_task_id);
                } else {
                    LOG_WARN("BLE", "CCCD alloc failed");
                    s_disc_state = DISC_IDLE;
                }
            }
        }
    }
    else if (s_disc_state == DISC_CCCD) {
        if (pMsg->method == ATT_WRITE_RSP) {
            LOG_INFO("BLE", "HID notifications enabled");
            /* 下一步: 发现电池服务 */
            s_disc_state = DISC_BAT_SVC;
            uint8_t uuid[ATT_BT_UUID_SIZE] = {
                LO_UINT16(BATTERY_SERVICE_UUID),
                HI_UINT16(BATTERY_SERVICE_UUID)
            };
            GATT_DiscPrimaryServiceByUUID(g_conn_handle, uuid,
                                          ATT_BT_UUID_SIZE, s_task_id);
        } else if (pMsg->method == ATT_ERROR_RSP) {
            LOG_WARN("BLE", "CCCD write err=0x%02x (notify unavailable)",
                     pMsg->msg.errorRsp.errCode);
            s_disc_state = DISC_IDLE;
        }
    }
    else if (s_disc_state == DISC_BAT_SVC) {
        /* 保存服务范围, 等 bleProcedureComplete 再发起特征发现.
         * 与 DISC_SVC 模式一致 — 不能在过程完成前链式调用下一个 GATT 操作,
         * 否则 WCH 栈会因前一个过程未结束而丢弃新请求. */
        if (pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP &&
            pMsg->msg.findByTypeValueRsp.numInfo > 0) {
            s_svc_start = ATT_ATTR_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
            s_svc_end   = ATT_GRP_END_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
            LOG_INFO("BLE", "Battery svc: 0x%X-0x%X", s_svc_start, s_svc_end);
        }
        if ((pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP &&
             pMsg->hdr.status == bleProcedureComplete) ||
            pMsg->method == ATT_ERROR_RSP) {
            if (s_svc_start != 0) {
                /* 用 GATT_DiscAllChars 替代 GATT_ReadUsingCharUUID:
                 * ReadUsingCharUUID 对短范围(4个handle)可能返回异常,
                 * DiscAllChars 读取特征声明, 与 HID 服务发现方式一致, 更可靠. */
                s_disc_state = DISC_BAT_CHAR;
                g_stadia_bat_handle = 0;
                s_bat_cccd_handle = 0;
                GATT_DiscAllChars(g_conn_handle, s_svc_start, s_svc_end, s_task_id);
            } else {
                LOG_INFO("BLE", "No battery service");
                s_disc_state = DISC_IDLE;
            }
        }
    }
    else if (s_disc_state == DISC_BAT_CHAR) {
        /* GATT_DiscAllChars 返回 ATT_READ_BY_TYPE_RSP (与 HID 发现相同格式)
         * 每对: declHandle(2) + properties(1) + valueHandle(2) + UUID(2) */
        if (pMsg->method == ATT_READ_BY_TYPE_RSP &&
            pMsg->msg.readByTypeRsp.numPairs > 0) {
            uint8_t *pData = pMsg->msg.readByTypeRsp.pDataList;
            for (int i = 0; i < pMsg->msg.readByTypeRsp.numPairs; i++) {
                uint8_t pairLen = pMsg->msg.readByTypeRsp.len;
                uint8_t  properties = pData[2];          /* 特征属性 */
                uint16_t valueHandle = BUILD_UINT16(pData[3], pData[4]);
                uint16_t uuid = BUILD_UINT16(pData[5], pData[6]);
                if (uuid == BATTERY_LEVEL_UUID) {
                    g_stadia_bat_handle = valueHandle;
                    s_bat_cccd_handle = (properties & 0x30) ? (valueHandle + 1) : 0;
                    LOG_INFO("BLE", "Battery level: 0x%X props=0x%02X cccd=%s",
                             valueHandle, properties,
                             (properties & 0x30) ? "yes" : "no");
                }
                pData += pairLen;
            }
        }
        if ((pMsg->method == ATT_READ_BY_TYPE_RSP &&
             pMsg->hdr.status == bleProcedureComplete) ||
            pMsg->method == ATT_ERROR_RSP) {
            if (g_stadia_bat_handle > 0) {
                if (s_bat_cccd_handle > 0) {
                    /* 特征支持通知/指示 → 使能 CCCD */
                    s_disc_state = DISC_BAT_CCCD;
                    attWriteReq_t wr;
                    memset(&wr, 0, sizeof(wr));
                    wr.cmd    = FALSE;
                    wr.sig    = FALSE;
                    wr.handle = s_bat_cccd_handle;
                    wr.len    = 2;
                    wr.pValue = GATT_bm_alloc(g_conn_handle, ATT_WRITE_REQ, 2, NULL, 0);
                    if (wr.pValue) {
                        wr.pValue[0] = 0x01;
                        wr.pValue[1] = 0x00;
                        GATT_WriteCharValue(g_conn_handle, &wr, s_task_id);
                    }
                } else {
                    /* 不支持通知 → 跳过 CCCD, 直接读一次当前电量 */
                    s_disc_state = DISC_IDLE;
                    g_stadia_bat_read_pending = true;
                    attReadReq_t rd;
                    memset(&rd, 0, sizeof(rd));
                    rd.handle = g_stadia_bat_handle;
                    GATT_ReadCharValue(g_conn_handle, &rd, s_task_id);
                }
            } else {
                LOG_INFO("BLE", "Battery level char not found (ignore)");
                s_disc_state = DISC_IDLE;
            }
        }
    }
    else if (s_disc_state == DISC_BAT_CCCD) {
        if (pMsg->method == ATT_WRITE_RSP || pMsg->method == ATT_ERROR_RSP) {
            if (pMsg->method == ATT_ERROR_RSP)
                LOG_WARN("BLE", "CCCD write err=0x%02x (notify unavailable)",
                         pMsg->msg.errorRsp.errCode);
            /* CCCD 成功后也不需要额外操作 — 电池通知会在 GATTMsgCB 自动处理 */
            s_disc_state = DISC_IDLE;
            LOG_INFO("BLE", "All services ready!");
        }
    }
}

/* ---- 添加设备到扫描列表 (参考 central.c centralAddDeviceInfo) ---- */
static void stadia_AddDevice(uint8_t *pAddr, uint8_t addrType)
{
    uint8_t i;
    if (s_scan_res >= DEFAULT_SCAN_RES_MAX) return;

    /* 去重 */
    for (i = 0; i < s_scan_res; i++) {
        if (tmos_memcmp(pAddr, s_dev_list[i].addr, B_ADDR_LEN))
            return;
    }
    /* 添加 */
    tmos_memcpy(s_dev_list[s_scan_res].addr, pAddr, B_ADDR_LEN);
    s_dev_list[s_scan_res].addrType = addrType;
    s_scan_res++;
}

/* ---- Discovery 超时强制完成 (防止电池发现卡死阻塞后续操作) ---- */
void StadiaBLE_ForceDiscoveryDone(void)
{
    if (s_disc_state != DISC_IDLE) {
        LOG_WARN("BLE", "Discovery timeout, force complete (disc=%d)", s_disc_state);
        s_disc_state = DISC_IDLE;
    }
}
