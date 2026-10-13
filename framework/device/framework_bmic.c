#include "framework_bmic.h"
#include "framework_log.h"
#include "framework_config.h"
#include "framework_msg.h"
#include "osi_api.h"
#include "typedef.h"
#include "mfpr_api.h"
#include "mfpr.h"
#include "gpio_api.h"
#include "drv_i2c.h"

#define TAG "framework_bmic"

int pp_bmic_write_byte(uint8_t cmd, uint8_t data);

typedef struct {
    uint8_t reg;
    uint8_t value;
    const char *desc;
} charger_config_t;

typedef struct {
    const char* ic_name;
    uint8_t slv_addr;
    uint8_t reg_addr;
    uint8_t disable_value;
    uint8_t enable_value;
} charger_modeset_t;

static const charger_modeset_t charger_configs[] = {
#ifdef SD7601D_CHARGER_ADDR
    {"SD7601D", SD7601D_CHARGER_ADDR, 0x07, 0x64, 0x44},
#endif
#ifdef SCV89602D_CHARGER_ADDR
    {"SCV89602D", SCV89602D_CHARGER_ADDR, 0x3b, 0x0a, 0x08},
#endif
#ifdef SGM41512S_CHARGER_ADDR
    {"SGM41512S", SGM41512S_CHARGER_ADDR, 0x07, 0x64, 0x44},
#endif
};

#ifdef SD7601D_CHARGER_ADDR
static charger_config_t SD7601D_def_configs[] = {
    {0x00, 0x04, "REG00, bit0~4, IINDPM, Input Current Limit"},
    {0x01, 0x1a, "REG01, bit1~3, SYS_Min"},
    {0x02, 0xa8, "REG02, bit0~5, ICHG 2400mA, Fast Charge Current"},
    {0x03, 0x2f, "REG03, bit0~4, ITERM 960mA, Termination Current, bit1~7, IPRECHG, Precharge Current"},
    {0x04, 0x59, "REG04, bit3~7, VREG 4.208V, Charge Voltage, VRECHG 256mV"},
    {0x05, 0x9b, "REG05, CHG_TIMER 5h"},
    {0x06, 0xe6, "REG06"},
    {0x07, 0x4c, "REG07"},
};

static void SD7601D_set_def_value(void)
{
    int ret;
    uint8_t cmd;
    uint8_t data[3] = {0};

    int config_count = sizeof(SD7601D_def_configs) / sizeof(SD7601D_def_configs[0]);

    for (int i = 0; i < config_count; i++) {
        cmd = SD7601D_def_configs[i].reg;
        data[0] = SD7601D_def_configs[i].value;
        ret = drvI2cWrite(BMIC_I2C_NUMBER, SD7601D_CHARGER_ADDR, &cmd, 1, data, 1);
        if (ret != 0) {
            OSI_LOGW(TAG, "[SD7601D] write reg:0x%x data:0x%x. Desc:%s", cmd, data[0], SD7601D_def_configs[i].desc);
        }
    }
}
#endif

#ifdef SGM41512S_CHARGER_ADDR
static charger_config_t SGM41512S_def_configs[] = {
    {0x00, 0x1f, "REG00"},
    {0x01, 0x1a, "REG01"},
    {0x02, 0xb8, "REG02"}, /*ICHG 2460mA*/
    {0x03, 0xaf, "REG03"}, /*ITERM 240mA*/
    {0x04, 0x49, "REG04"}, /*VREG 4.112V, VRECHG 200mV*/
    {0x05, 0x8b, "REG05"}, /*Charge Safety Timer 16h -> 7h*/
    {0x06, 0xe6, "REG06"},
    {0x10, 0x00, "REG10"} /*ITERM_PRE_SEL 0, ITERM * 1*/
};

static void SGM41512S_set_def_value(void)
{
    int ret;
    uint8_t cmd;
    uint8_t data[3] = {0};

    int config_count = sizeof(SGM41512S_def_configs) / sizeof(SGM41512S_def_configs[0]);

    for (int i = 0; i < config_count; i++) {
        cmd = SGM41512S_def_configs[i].reg;
        data[0] = SGM41512S_def_configs[i].value;
        ret = drvI2cWrite(BMIC_I2C_NUMBER, SGM41512S_CHARGER_ADDR, &cmd, 1, data, 1);
        if (ret != 0) {
            OSI_LOGW(TAG, "[SGM41512S] write reg:0x%x data:0x%x. Desc:%s", cmd, data[0], SGM41512S_def_configs[i].desc);
        }
    }
}
#endif

static void prv_BATFET_Disable_Mode(uint8_t value)
{
    int ret;
    uint8_t cmd;
    uint8_t data[3] = {0};

    if (sizeof(charger_configs) / sizeof(charger_configs[0]) == 0) {
        OSI_LOGE(TAG, "[ERROR] no match charge ic");
        return;
    }

    for (int j = 0; j < (int)(sizeof(charger_configs) / sizeof(charger_configs[0])); j++) {
        const charger_modeset_t* config = &charger_configs[j];
        const char* desc = value ? "BATFET Disable Mode" : "BATFET Enable Mode";
        uint8_t reg_value = value ? config->disable_value : config->enable_value;

        cmd = config->reg_addr;
        data[0] = reg_value;

        ret = drvI2cWrite(BMIC_I2C_NUMBER, config->slv_addr, &cmd, 1, data, 1);
        if (ret != 0) {
            OSI_LOGW(TAG, "[%s] write reg:0x%02x data:0x%02x. Desc:%s", config->ic_name, cmd, data[0], desc);
        }
    }
}

void ChargeIC_Shipping_Mode(void)
{
    prv_BATFET_Disable_Mode(1);
}

void ChargeIC_Exit_Shipping_Mode(void)
{
    prv_BATFET_Disable_Mode(0);
}

/**
 * @brief  Charging Status
                00 = Charge disable
                01 = Pre-charge (VBAT < VBATLOW)
                10 = Fast charging (constant current or voltage)
                11 = Charging terminated
 * @note
 * @retval
 */
uint8_t ChargeIC_ChargeStatus(void)
{
    int ret = 0;
    uint8_t slv_addr = 0;
    uint8_t cmd = 0;
    uint8_t data[3] = {0};
    uint8_t mask = 0;
    uint8_t mask_shift = 0;
#ifdef SGM41512S_CHARGER_ADDR
    slv_addr = SGM41512S_CHARGER_ADDR;
    cmd = 0x08;
    mask = 0x18;
    mask_shift = 3;
#endif
    ret = drvI2cRead(BMIC_I2C_NUMBER, slv_addr, &cmd, 1, data, 1);
    if (ret != 0) {
        RTI_LOG("read reg:0x%02x data:0x%02x.", cmd, data[0]);
        return -1;
    }
    return ((data[0] & mask) >> mask_shift);
}

/* CRC-8/SMBUS table */
static const uint8_t crc8_smbus_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

static uint8_t crc8_smbus(uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    while (len--) {
        crc = crc8_smbus_table[crc ^ *data++];
    }
    return crc;
}

#if BATTERY_PERCENT_SOURCE_BMIC
static osiTimer_t *s_bmic_percent_timer = NULL;
static osiWork_t *s_bmic_percent_work = NULL;
static uint8_t s_bmic_last_percent = 0;
static uint8_t s_bmic_last_reported_percent = 0xFF;
static uint16_t s_bmic_last_mv = 0;

static void prv_bmic_report_percent(uint8_t percent, uint16_t mv)
{
    if (s_bmic_last_reported_percent == percent) {
        return;
    }

    framework_msg_t msg = {0};
    msg.id = MSG_CHARGE_PERCENT_CHANGE;
    msg.param1 = percent;
    msg.param2 = s_bmic_last_reported_percent;
    msg.param3 = mv;
    framework_msg_send(&msg);
    OSI_LOGI(TAG, "BMIC percent changed: %u -> %u (mv=%u)", s_bmic_last_reported_percent, percent, mv);
    s_bmic_last_reported_percent = percent;
}

static void prv_bmic_percent_timer_handler(void *ctx)
{
    (void)ctx;
    if (s_bmic_percent_work) {
        osiWorkEnqueue(s_bmic_percent_work, osiSysWorkQueueLowPriority());
    } else {
        OSI_LOGE(TAG, "BMIC percent work not created");
    }

    if (s_bmic_percent_timer) {
        osiTimerStart(s_bmic_percent_timer, BATTERY_PERCENT_REPORT_MS);
    }
}

static void prv_bmic_percent_work(void *ctx)
{
    (void)ctx;
    uint16_t mv = s_bmic_last_mv;
    uint8_t percent = s_bmic_last_percent;

    if (framework_bmic_read_voltage(&mv) == 0) {
        s_bmic_last_mv = mv;
    }

    if (framework_bmic_read_soc(&percent) == 0) {
        s_bmic_last_percent = percent;
        OSI_LOGI(TAG, "BMIC percent read: %u (mv=%u)", percent, mv);
        prv_bmic_report_percent(percent, s_bmic_last_mv);
    }
}
#endif

static uint8_t bmic_set_def_value(void)
{
    return (uint8_t)pp_bmic_write_byte(CMD_SLEEP_TIME, 0);
}

void pp_module_init(void)
{
    int ret;
    unsigned int mfp_cfg_io[] = {
        MFP_REG(I2C_SCL) | MFP_AF1 | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
        MFP_REG(I2C_SDA) | MFP_AF1 | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,

        MFP_REG(WAKEUP_PORT) | MFP_AF0 | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
        MFP_EOC
    };
    mfp_config(mfp_cfg_io);
    GpioSetDirection(WAKEUP_PORT_OUTPUT, GPIO_OUT_PIN);
    GpioSetLevel(WAKEUP_PORT_OUTPUT, 0);
    drvI2cInit(BMIC_I2C_NUMBER, 0);
    OSI_LOGI(TAG, "bmic init end");
    uint8_t cmd = 0x0f;
    uint8_t data[3] = {0};
    ret = drvI2cRead(BMIC_I2C_NUMBER, SD77428_COULOMB_COUNTER_ADDR, (uint8_t *)&cmd, 1, data, 1);
    if (ret != 0) {
        OSI_LOGW(TAG, "[BMIC] pp_bmic_read cmd:0x%x read error.", cmd);
    }
#ifdef SD7601D_CHARGER_ADDR
    SD7601D_set_def_value();
#endif
#ifdef SGM41512S_CHARGER_ADDR
    SGM41512S_set_def_value();
#endif
    bmic_set_def_value();
}

uint8_t pp_bmic_read_SOH_percent(void)
{
    uint8_t ret = 0;
    uint8_t data[3] = {0};
    uint8_t cmd = CMD_StateOfHealtPercent;
    ret = drvI2cRead(BMIC_I2C_NUMBER, BMIC_SLAVE_ADDR, (uint8_t *)&cmd, 1, data, 3);
    if (ret == (uint8_t)-1) {
        OSI_LOGW(TAG, "[BMIC] SOH percent read error.");
        return ret;
    }
    ret = (uint8_t)(data[0] | (data[1] << 8));
    if (ret > 100) {
        OSI_LOGW(TAG, "[BMIC] SOH percent value (0x%x) is out of range.", ret);
    }
    return ret;
}

/**
 * @brief  returns an unsigned integer value of the predicted remaining battery
            capacity expressed as a percentage of FullChargeCapacity()
 * @note
 * @retval 0-100, unit %
 */
uint8_t pp_bmic_read_SOC_percent(void)
{
    uint8_t ret = 0;
    uint8_t data[3] = {0};
    uint8_t cmd = 0x1c;
    ret = drvI2cRead(BMIC_I2C_NUMBER, BMIC_SLAVE_ADDR, (uint8_t *)&cmd, 1, data, 3);
    if (ret == -1) {
        RTI_LOG("[BMIC]SOC percent read error.");
        return ret;
    }
    ret = data[0] | (data[1] << 8);
    if (ret > 100) {
        RTI_LOG("[BMIC]SOC percent value (0x%x) is out of range.", ret);
    }
    return ret;
}

int pp_bmic_read_word(uint8_t cmd, uint16_t *data)
{
    int ret = 0;
    uint8_t value[3] = {0};
    ret = drvI2cRead(BMIC_I2C_NUMBER, BMIC_SLAVE_ADDR, (uint8_t *)&cmd, 1, (uint8_t *)&value, 3);
    if (ret != 0) {
        OSI_LOGW(TAG, "[BMIC] pp_bmic_read_word cmd:0x%x read error.", cmd);
    }
    uint8_t crc_value[] = {BMIC_SLAVE_ADDR<<1, cmd, (uint8_t)(BMIC_SLAVE_ADDR<<1 | 1), value[0], value[1]};
    uint8_t crc_result = crc8_smbus(crc_value, sizeof(crc_value) / sizeof(crc_value[0]));
    if (value[2] != crc_result) {
        OSI_LOGW(TAG, "[BMIC] CRC check error:%02x", value[2]);
        ret = -1;
        return ret;
    }
    *data = (uint16_t)(value[0] | (value[1] << 8));

    return ret;
}

int pp_bmic_write_word(uint8_t cmd, uint16_t data)
{
    int ret = 0;
    uint8_t write_buf[3];
    write_buf[0] = data & 0xFF;
    write_buf[1] = (data >> 8) & 0xFF;
    uint8_t crc_value[] = {BMIC_SLAVE_ADDR<<1, cmd, write_buf[0], write_buf[1]};
    write_buf[2] = crc8_smbus(crc_value, sizeof(crc_value) / sizeof(crc_value[0]));
    ret = drvI2cWrite(BMIC_I2C_NUMBER, BMIC_SLAVE_ADDR, (uint8_t *)&cmd, 1, write_buf, sizeof(write_buf) / sizeof(write_buf[0]));
    if (ret != 0) {
        OSI_LOGW(TAG, "[BMIC] pp_bmic_write_word cmd:0x%x write error.", cmd);
    }

    return ret;
}

int pp_bmic_write_byte(uint8_t cmd, uint8_t data)
{
    int ret = 0;
    uint8_t write_buf[2];
    write_buf[0] = data & 0xFF;
    uint8_t crc_value[] = {BMIC_SLAVE_ADDR<<1, cmd, write_buf[0]};
    write_buf[1] = crc8_smbus(crc_value, sizeof(crc_value) / sizeof(crc_value[0]));
    ret = drvI2cWrite(BMIC_I2C_NUMBER, BMIC_SLAVE_ADDR, (uint8_t *)&cmd, 1, write_buf, sizeof(write_buf) / sizeof(write_buf[0]));
    if (ret != 0) {
        OSI_LOGW(TAG, "[BMIC] pp_bmic_write_byte cmd:0x%x write error.", cmd);
    }

    return ret;
}

int pp_bmic_shutdown(void)
{
    int ret;
    uint8_t cmd = 0x00;
    uint16_t data = 0x001B;
    ret = pp_bmic_write_word(cmd, data);
    if (ret != 0) {
        OSI_LOGW(TAG, "[BMIC] cmd:0x%x data:0x%x write error.", cmd, data);
        return ret;
    }
    data = 0x001C;
    ret = pp_bmic_write_word(cmd, data);
    if (ret != 0) {
        OSI_LOGW(TAG, "[BMIC] cmd:0x%x data:0x%x write error.", cmd, data);
        return ret;
    }
    return ret;
}

int pp_bmic_wakeup(void)
{
    GpioSetLevel(WAKEUP_PORT_OUTPUT, 1);
    osiThreadSleep(1);
    GpioSetLevel(WAKEUP_PORT_OUTPUT, 0);
    return 0;
}

int framework_bmic_init(void)
{
    pp_module_init();
#if BATTERY_PERCENT_SOURCE_BMIC
    if (framework_bmic_read_voltage(&s_bmic_last_mv) != 0) {
        s_bmic_last_mv = 0;
    }

    uint8_t init_pct = 0;
    if (framework_bmic_read_soc(&init_pct) == 0) {
        s_bmic_last_percent = init_pct;
        s_bmic_last_reported_percent = 0xFF;  // force initial report
    } else {
        s_bmic_last_percent = 0;
        s_bmic_last_reported_percent = 0xFF;
    }

    s_bmic_percent_work = osiWorkCreate(prv_bmic_percent_work, NULL, NULL);
    if (s_bmic_percent_work == NULL) {
        OSI_LOGE(TAG, "Create BMIC percent work failed");
    }

    s_bmic_percent_timer = osiTimerCreate(prv_bmic_percent_timer_handler, NULL);
    if (s_bmic_percent_timer) {
        osiTimerStart(s_bmic_percent_timer, BATTERY_PERCENT_REPORT_MS);
    } else {
        OSI_LOGE(TAG, "Create BMIC percent timer failed");
    }
#endif
    return 0;
}

int framework_bmic_sleep(void)
{
    return pp_bmic_shutdown();
}

int framework_bmic_wakeup(void)
{
    return pp_bmic_wakeup();
}

int framework_bmic_shipping_mode_enable(void)
{
    ChargeIC_Shipping_Mode();
    return 0;
}

int framework_bmic_shipping_mode_disable(void)
{
    ChargeIC_Exit_Shipping_Mode();
    return 0;
}

int framework_bmic_read_voltage(uint16_t *millivolt)
{
    if (!millivolt) {
        return -1;
    }
    uint16_t raw = 0;
    if (pp_bmic_read_word(CMD_Voltage, &raw) != 0) {
        return -2;
    }
    *millivolt = raw;
    return 0;
}

int framework_bmic_read_temperature(int *temperature)
{
    if (!temperature) {
        return -1;
    }
    uint16_t raw = 0;
    if (pp_bmic_read_word(CMD_INTERNAL_TEMPERATURE, &raw) != 0) {
        return -2;
    }
    // K转celsius
    *temperature = (raw*10 - 27315)/100;
    return 0;
}

int framework_bmic_read_soh(uint8_t *percent)
{
    if (!percent) {
        return -1;
    }
    *percent = pp_bmic_read_SOH_percent();
    return 0;
}

int framework_bmic_read_soc(uint8_t *percent)
{
    if (!percent) {
        return -1;
    }
    *percent = pp_bmic_read_SOC_percent();
    return 0;
}

