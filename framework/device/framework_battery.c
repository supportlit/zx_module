/**
 * @file framework_battery.c
 * @brief Framework Battery Management Module Implementation
 * @details Provides battery voltage monitoring, charging state management,
 *          and battery capacity calculation based on customer-specific tables
 */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "framework_config.h"
#include "framework_battery.h"
#include "framework_msg.h"
#include "framework_log.h"
#include "osi_api.h"
#include "gpio_api.h"
#include "drv_adc.h"
#include "drv_iomux.h"
#include "drv_def.h"

#define TAG "FRAMEWORK_BATTERY"

// External HAL declarations
extern bool CommPMVbusDetect(void);

// ============================================================================
// Macro Definitions
// ============================================================================
// Note: charge_status_e is defined in framework_msg.h:
// CHARGE_STATUS_NONE = 0, CHARGE_STATUS_CHARGING = 1,
// CHARGE_STATUS_FULL = 2, CHARGE_STATUS_LOW = 3

// ============================================================================
// Static Variables
// ============================================================================
// Battery configuration (provided by customer layer)
static const battery_config_t *s_battery_config = NULL;

static uint32_t *s_bat_voltage_array = NULL;  // Voltage sliding average array[20]
static uint32_t s_cur_bat_voltage = 0;        // Current battery voltage (mV)
static bool s_is_charging = false;             // Charging state flag
static uint8_t s_last_percent = 0;             // Last battery percentage
static uint8_t s_last_reported_percent = 0xFF; // Last reported percent
static uint8_t first_chg_state = 0;

static osiTimer_t *s_lowpower_timer = NULL;  // low power timer
static uint32_t s_lowpower_interval_low = 10*60*1000; // low power timer interval
static uint32_t s_lowpower_interval_high = 30*60*1000; // low power timer interval
static device_sleep_callback_t sleep_callback = NULL; // Sleep callback function
// ============================================================================
// External function declarations (provided by HAL layer)
// ============================================================================

// ============================================================================
// Message sending using unified framework_msg_send
// ============================================================================


// ============================================================================
// Internal Function Declarations
// ============================================================================
static uint32_t prv_get_adc_average(void);
static charge_status_e prv_get_charge_status(void);
static void prv_bat_voltage_update(charge_status_e cur_status);
static void prv_usb_plug_detect_task(void *param);
static void prv_lowpower_30m_timer_handler(void *ctx);
static void prv_lowpower_10m_timer_handler(void *ctx);
static void prv_report_percent(uint8_t current_percent);

// ============================================================================
// Internal Function Implementations
// ============================================================================

static void prv_gpio_init(void){
    unsigned int mfp_spk[] = {
        MFP_REG(CHARGER_STDBY_PINMUX)   | CHARGER_STDBY_PINFUN | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(CHARGER_DETECT_PINMUX) | CHARGER_DETECT_PINFUN | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_EOC
    };
    drvGpioPinMuxConfig(mfp_spk);
}

/**
 * @brief Sample ADC and calculate average voltage (excluding max/min)
 * @details Sample 10 times, exclude max/min values, then calculate average
 * @return Average voltage (mV)
 */
static uint32_t prv_get_adc_average(void)
{
    uint32_t adc_val = 0;
    uint32_t adc_read = 0;
    uint32_t max_val = 0;
    uint32_t min_val = 9000;

    for (int i = 0; i < ADC_SAMPLE_CNT; i++) {
        adc_read = drvVbatRead();
        
        if (adc_read >= max_val) {
            max_val = adc_read;
        }
        if (adc_read <= min_val) {
            min_val = adc_read;
        }
        
        adc_val += adc_read;
        osiThreadSleepMs(10);
    }

    // Remove max/min values and calculate average
    adc_val -= min_val;
    adc_val -= max_val;

    return adc_val / (ADC_SAMPLE_CNT - 2);
}

/**
 * @brief Calculate sliding average voltage
 * @param vol_list Voltage array
 * @param size Array size
 * @return Average voltage (mV)ddd
 */
static uint32_t prv_calc_avg_voltage(uint32_t *vol_list, int size)
{
    uint32_t sum = 0;
    for (int i = 0; i < size; i++) {
        sum += vol_list[i];
    }
    return (sum / size);
}

/**
 * @brief Send low power warning message
 */
static void prv_send_lowpower_message(void)
{
    framework_msg_t msg = {0};
    msg.id = MSG_CHARGE_LOW_POWER;
    msg.param1 = 1;
    msg.param2 = 1;
    framework_msg_send(&msg);
}

static void prv_report_percent(uint8_t current_percent)
{
    if (s_last_reported_percent == current_percent) {
        return;
    }
    framework_msg_t msg = {0};
    msg.id = MSG_CHARGE_PERCENT_CHANGE;
    msg.param1 = current_percent;            /* 当前电量百分比 */
    msg.param2 = s_last_reported_percent;    /* 上次电量百分比 */
    msg.param3 = s_cur_bat_voltage;          /* 当前电压 */
    framework_msg_send(&msg);
    OSI_LOGI(TAG, "Battery percent changed: %u -> %u (mv=%u)", s_last_reported_percent, current_percent, s_cur_bat_voltage);
    s_last_reported_percent = current_percent;
}

/**
 * @brief 30min low power timer callback
 */
static void prv_lowpower_timer_handler_lower(void *ctx)
{
    (void)ctx;
    osiTimerStop(s_lowpower_timer);
    prv_send_lowpower_message();
    osiTimerStart(s_lowpower_timer, s_lowpower_interval_low);
}

static void prv_lowpower_timer_handler_higher(void *ctx)
{
    (void)ctx;
    osiTimerStop(s_lowpower_timer);
    prv_send_lowpower_message();
    osiTimerStart(s_lowpower_timer, s_lowpower_interval_high);
}

/**
 * @brief Get current charging status
 * @details Detects VBUS + CHARGER_DETECT_PIN + CHARGER_STDBY_PIN states
 * @return Charge status enumeration value
 */
static charge_status_e prv_get_charge_status(void)
{
    // Check if VBUS is present
    if (CommPMVbusDetect() == false) {
        return CHARGE_STATUS_NONE;
    }

    #ifdef FRAMEWORK_BATTERY_CHARGE_DETECT_USE_VBUS
    return CHARGE_STATUS_CHARGING;
    #else
    int val_chg = 1;
    int val_std = 1;

    // Read charge management chip status pins
    val_std = GpioGetLevel(CHARGER_STDBY_PIN);  // STDBY pin
    val_chg = GpioGetLevel(CHARGER_DETECT_PIN); // CHG pin

    if (val_std == 1 && val_chg == 1) {
        // Both pins high -> USB plugged but not charging
        return CHARGE_STATUS_NONE;
    } else if (val_std == 0 && val_chg == 1) {
        // STDBY=0, CHG=1 -> Charging complete
        return CHARGE_STATUS_FULL;
    }

    // STDBY=1, CHG=0 or STDBY=0, CHG=0 -> Charging in progress
    return CHARGE_STATUS_CHARGING;
    #endif
}

/**
 * @brief Update battery voltage (sliding average + low battery detection)
 * @param cur_status Current charging status
 */
static void prv_bat_voltage_update(charge_status_e cur_status)
{
    static bool pre_chg_state = false;
    static bool first_state = true;
    static int write_idx = 0;
    uint32_t vol = 0;
    framework_msg_t msg = {0};
    int get_loop = 1;

    // Fast fill voltage array when charge/discharge state changes
    if (pre_chg_state != s_is_charging) {
        get_loop = BAT_DETECT_ARRAY_SIZE / 2;
        pre_chg_state = s_is_charging;
    }

    // Update voltage sliding array
    for (int i = 0; i < get_loop; i++) {
        s_bat_voltage_array[write_idx++] = prv_get_adc_average();
        if (write_idx >= BAT_DETECT_ARRAY_SIZE) {
            write_idx = 0;
        }
    }

    // Calculate average voltage
    vol = prv_calc_avg_voltage(s_bat_voltage_array, BAT_DETECT_ARRAY_SIZE);
    if (vol == 0) {
        return;
    }

    // Update current voltage (only increase when charging, only decrease when discharging)
    if (s_is_charging) {
        if (vol > s_cur_bat_voltage) {
            s_cur_bat_voltage = vol;
        }
    } else {
        if (vol < s_cur_bat_voltage) {
            s_cur_bat_voltage = vol;
        }

        // Low battery detection logic (discharge mode only)
        if (first_state) {
            // First check: if voltage below minimum threshold, shutdown immediately
            uint16_t min_voltage = s_battery_config->discharge_table[s_battery_config->discharge_table_size - 1][0];
            OSI_LOGI(TAG, "Battery voltage check first state: %d < %d", s_cur_bat_voltage, min_voltage);
            if (s_cur_bat_voltage < min_voltage) {
                msg.id = MSG_CHARGE_LOW_POWER;
                msg.param1 = 1;
                msg.param2 = -1;  // -1 indicates immediate shutdown
                framework_msg_send(&msg);
            }
        } else {
            // 3600-3574mV: Start 30-minute timer
            if (s_cur_bat_voltage > 3574 && s_cur_bat_voltage < 3600) {
                if (s_lowpower_timer) {
                    osiTimerStop(s_lowpower_timer);
                    osiTimerDelete(s_lowpower_timer);
                    s_lowpower_timer = NULL;
                }

                if (s_lowpower_timer == NULL) {
                    prv_send_lowpower_message();
                    s_lowpower_timer = osiTimerCreate(prv_lowpower_timer_handler_higher, s_lowpower_timer);
                    osiTimerStart(s_lowpower_timer, s_lowpower_interval_high);
                }
            }
            // 3574-3501mV: Start 10-minute timer
            else if (s_cur_bat_voltage <= 3574 && s_cur_bat_voltage > 3501) {
                // Stop 30-minute timer
                if (s_lowpower_timer) {
                    osiTimerStop(s_lowpower_timer);
                    osiTimerDelete(s_lowpower_timer);
                    s_lowpower_timer = NULL;
                }

                // Start 10-minute timer
                if (s_lowpower_timer == NULL) {
                    prv_send_lowpower_message();
                    s_lowpower_timer = osiTimerCreate(prv_lowpower_timer_handler_lower, s_lowpower_timer);
                    osiTimerStart(s_lowpower_timer, s_lowpower_interval_low);
                }
            }
            // Below minimum threshold: Shutdown immediately
            else if (s_cur_bat_voltage < s_battery_config->discharge_table[s_battery_config->discharge_table_size - 1][0]) {
                msg.id = MSG_CHARGE_LOW_POWER;
                msg.param1 = 1;
                msg.param2 = -1;
                framework_msg_send(&msg);
            }
        }
    }

    first_state = false;

    // 检查电量百分比是否变化，并发送事件通知（非BMIC模式）
#if !BATTERY_PERCENT_SOURCE_BMIC
    prv_report_percent(framework_battery_get_percent());
#endif
}

/**
 * @brief USB plug/unplug detection task (500ms polling)
 * @param param Task parameter (unused)
 */
static void prv_usb_plug_detect_task(void *param)
{
    (void)param;
    framework_msg_t msg = {0};
    charge_status_e cur_status = CHARGE_STATUS_NONE;
    charge_status_e pre_status = CHARGE_STATUS_NONE;
    static uint32_t last_msg_id = MSG_CHARGE_PLUGOUT;
    int cnt = 0;

    // Initialize charge detection GPIO
    GPIOConfiguration config = {0};
    config.pinDir = GPIO_IN_PIN;
    config.initLv = 0;
    config.pinPull = GPIO_PULLUP_ENABLE;
    config.pinEd = GPIO_NO_EDGE;
    config.isr = NULL;
    config.wu = NULL;
    GpioInitConfiguration(CHARGER_DETECT_PIN, config);
    GpioInitConfiguration(CHARGER_STDBY_PIN, config);

    // Initialize voltage array
    for (int i = 0; i < BAT_DETECT_ARRAY_SIZE; i++) {
        s_bat_voltage_array[i] = s_cur_bat_voltage;
    }

    osiThreadSleepMs(1000);  // Delay 1 second before starting
    s_is_charging = CommPMVbusDetect();
    prv_bat_voltage_update(cur_status);
    first_chg_state = s_is_charging;

    // Main loop
    while (1) {
        cur_status = prv_get_charge_status();
        #ifdef FRAMEWORK_BATTERY_CHARGE_DETECT_USE_VBUS
        if(cur_status == CHARGE_STATUS_CHARGING && framework_battery_get_percent() >= 100){
            cur_status = CHARGE_STATUS_FULL;
        }
        #endif

        // No state change, periodically update voltage
        if (cur_status == pre_status) {
            cnt++;
            if (cnt >= READ_ADC_CNT) {
                cnt = 0;
                prv_bat_voltage_update(cur_status);
            }
            osiThreadSleepMs(USB_DETECT_INTERVAL);
            continue;
        }

        // State changed, send message
        pre_status = cur_status;
        msg.id = 0;

        switch (cur_status) {
        case CHARGE_STATUS_NONE:
            if (last_msg_id != MSG_CHARGE_PLUGOUT) {
                msg.id = MSG_CHARGE_PLUGOUT;
                msg.param1 = 1;
                last_msg_id = MSG_CHARGE_PLUGOUT;
            }
            s_is_charging = false;
            break;

        case CHARGE_STATUS_CHARGING:
            // Stop low power timers when charging
            if (s_lowpower_timer) {
                osiTimerStop(s_lowpower_timer);
                osiTimerDelete(s_lowpower_timer);
                s_lowpower_timer = NULL;
            }

            if(first_chg_state && first_chg_state) {
                first_chg_state = 0;
                msg.param1 = 1;
                msg.id = MSG_CHARGE_PLUGIN;
            }
            if (s_is_charging == false && last_msg_id == MSG_CHARGE_PLUGOUT) {
                msg.param1 = 1;
                msg.id = MSG_CHARGE_PLUGIN;
            } else if (last_msg_id == MSG_CHARGE_FULL) {
                msg.id = 0;  // Transition from full to charging, no message
                break;
            }
            last_msg_id = MSG_CHARGE_PLUGIN;
            s_is_charging = true;
            break;

        case CHARGE_STATUS_FULL:
            if (last_msg_id == MSG_CHARGE_PLUGIN) {
                msg.id = MSG_CHARGE_FULL;
                msg.param1 = 0;
                msg.param2 = s_cur_bat_voltage;
                last_msg_id = MSG_CHARGE_FULL;
            } else if (last_msg_id == MSG_CHARGE_PLUGOUT) {
                msg.id = MSG_CHARGE_PLUGIN;
                msg.param1 = 1;
                last_msg_id = MSG_CHARGE_PLUGIN;
                pre_status = CHARGE_STATUS_CHARGING;
            }
            s_is_charging = true;
            break;

        default:
            break;
        }

        if (msg.id != 0) {
            framework_msg_send(&msg);
            cnt = 0;
            prv_bat_voltage_update(cur_status);
        }
    }
}

// ============================================================================
// Public Interface Implementation
// ============================================================================

/**
 * @brief Register battery voltage-capacity configuration tables
 */
int framework_battery_register_config(const battery_config_t *config)
{
    if (config == NULL || 
        config->discharge_table == NULL || 
        config->charge_table == NULL ||
        config->discharge_table_size == 0 ||
        config->charge_table_size == 0) {
        return -1;
    }
    
    s_battery_config = config;
    return 0;
}

/**
 * @brief Initialize battery management module
 */
int framework_battery_init(void)
{
    // Check if config is registered
    if (s_battery_config == NULL) {
        OSI_LOGE(TAG, "Battery config not registered");
        return -1;  // Config not registered
    }
    
    // Check if already initialized
    if (s_bat_voltage_array != NULL) {
        OSI_LOGE(TAG, "Battery module already initialized");
        return 0;  // Already initialized
    }

    prv_gpio_init();

    // Allocate voltage sliding average array
    s_bat_voltage_array = malloc(sizeof(uint32_t) * BAT_DETECT_ARRAY_SIZE);
    if (s_bat_voltage_array == NULL) {
        return -1;
    }
    memset(s_bat_voltage_array, 0, sizeof(uint32_t) * BAT_DETECT_ARRAY_SIZE);

    // Initialize voltage value
    s_cur_bat_voltage = prv_get_adc_average();
    s_last_percent = 0;
    s_last_reported_percent = 0xFF;
    s_is_charging = CommPMVbusDetect(); // Initial charging state
    first_chg_state = s_is_charging;

    #if !BATTERY_PERCENT_SOURCE_BMIC
    #else
    OSI_LOGI(TAG, "Battery initialized with voltage: %d[%d]", s_cur_bat_voltage,framework_battery_get_percent());
    #endif

    // Create USB detection task
    osiThread_t *usb_task = osiThreadCreate("usb_detect", 
                                            prv_usb_plug_detect_task, 
                                            NULL, 
                                            USB_DETECT_TASK_PRIORITY, 
                                            USB_DETECT_TASK_STACK_SIZE);
    if (usb_task == NULL) {
        OSI_LOGE(TAG, "Failed to create usb_detect task");
        return -1;
    }

    return 0;
}

int framework_battery_set_lowpower_intervals(uint32_t low_ms, uint32_t high_ms)
{
    if (low_ms == 0 || high_ms == 0 || low_ms > high_ms) {
        return -1;
    }

    s_lowpower_interval_low = low_ms;
    s_lowpower_interval_high = high_ms;

    return 0;
}

/**
 * @brief Get battery capacity percentage
 */
uint8_t framework_battery_get_percent(void)
{
    uint8_t percent = 0;
    // OSI_LOGI(TAG, "framework_battery_get_percent: voltage=%u, is_charging=%d", s_cur_bat_voltage, s_is_charging);

    if (s_cur_bat_voltage == 0) {
        return 0;
    }

    // Select voltage table based on charging/discharging state
    if (s_is_charging) {
        // Charging state - use charge voltage table
        for (int i = 0; i < s_battery_config->charge_table_size; i++) {
            if (s_cur_bat_voltage >= s_battery_config->charge_table[i][0]) {
                percent = s_battery_config->charge_table[i][1];
                
                // Linear interpolation calculation
                if (i > 0) {
                    percent = percent + (((s_cur_bat_voltage - s_battery_config->charge_table[i][0]) * 
                                         (s_battery_config->charge_table[i - 1][1] - s_battery_config->charge_table[i][1])) / 
                                        (s_battery_config->charge_table[i - 1][0] - s_battery_config->charge_table[i][0]));
                }

                // When charging, percentage only increases
                if (percent < s_last_percent) {
                    percent = s_last_percent;
                } else {
                    if(percent > s_last_percent + 1) {
                        if(s_last_percent != 0) {
                            percent = s_last_percent + 1; // Limit increase rate to 1% per read
                        }   
                    }
                    s_last_percent = percent;
                }

                // When charging, only return 100% when fully charged
                if (framework_battery_is_full()) {
                    s_last_percent = 100;
                    return 100;
                } else {
                    #ifdef FRAMEWORK_BATTERY_CHARGE_DETECT_USE_VBUS
                    return percent;
                    #else
                    if(percent == 100) {
                        s_last_percent = 99;
                        return 99;
                    }else {
                        return percent;
                    }
                    #endif
                }
            }
        }
        return 0;
    } else {
        // Discharging state - use discharge voltage table
        for (int i = 0; i < s_battery_config->discharge_table_size; i++) {
            if (s_cur_bat_voltage >= s_battery_config->discharge_table[i][0]) {
                percent = s_battery_config->discharge_table[i][1];
                
                // Linear interpolation calculation
                if (i > 0) {
                    percent = percent + (((s_cur_bat_voltage - s_battery_config->discharge_table[i][0]) * 
                                         (s_battery_config->discharge_table[i - 1][1] - s_battery_config->discharge_table[i][1])) / 
                                        (s_battery_config->discharge_table[i - 1][0] - s_battery_config->discharge_table[i][0]));
                }

                // When discharging, percentage only decreases
                if (percent > s_last_percent && s_last_percent != 0) {
                    percent = s_last_percent;
                } else {
                    if (percent < s_last_percent - 1)
                    {
                        percent = s_last_percent - 1; // Limit decrease rate to 1% per read
                    }
                    s_last_percent = percent;
                }

                if(percent  == 0) {
                    s_last_percent = 1;
                    return 0;
                }

                return percent;
            }
        }
        return 0;
    }
}

/**
 * @brief Get battery voltage
 */
uint32_t framework_battery_get_voltage(void)
{
    return s_cur_bat_voltage;
}

/**
 * @brief Query if battery is charging
 */
bool framework_battery_is_charging(void)
{
    return s_is_charging;
}

/**
 * @brief Query if USB is plugged in
 */
bool framework_battery_is_usb_plugin(void)
{
    return CommPMVbusDetect();
}

/**
 * @brief Query if battery is fully charged
 */
bool framework_battery_is_full(void)
{
    return (prv_get_charge_status() == CHARGE_STATUS_FULL);
}

/**
 * @brief Enable the device to enter sleep mode
 */
void framework_device_enable_sleep(uint8_t event, uint8_t enable)
{
    if(sleep_callback)
    {
        sleep_callback(event, enable);
    }
    else
    {
        osiSetPMEnableSleep(enable);
    }
}

/**
 * @brief Register device sleep callback function
 */
int framework_register_sleep_callback(device_sleep_callback_t callback)
{
    sleep_callback = callback;
    
    return 0;
}

uint32_t framework_factory_battery_voltage(void)
{
    return prv_get_adc_average();
}