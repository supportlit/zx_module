# template Soundbox SDK - Framework Documentation

## Overview

The template Soundbox SDK is a modular framework designed for IoT audio devices. It provides a flexible architecture that separates core framework functionality from customer-specific implementations.

**Key Features:**
- ✅ Modular architecture with clear separation of concerns
- ✅ Customer-specific configuration override system
- ✅ Multi-language audio resource management
- ✅ MQTT connectivity with auto-reconnection
- ✅ Battery monitoring and power management
- ✅ GPIO key handling with debouncing
- ✅ LED status indicators
- ✅ Device configuration persistence

---

## Architecture

### Design Principles

1. **Framework + Customer Model**: Generic framework with customer overrides
2. **Configuration Protection**: Use `#ifndef` macros for customer customization
3. **Callback Mechanism**: Return 0 for handled, -1 for default framework behavior
4. **Modular Design**: Independent modules (keys, battery, LED, messages, audio)

### Directory Structure

```
Code/sdk/
├── framework/                          # Framework Core Layer
│   ├── inc/
│   │   └── framework.h                 # Main framework header
│   ├── common/
│   │   ├── framework_config.h          # Hardware configuration (GPIO/timing/priority)
│   │   ├── framework_msg.h/c           # Message handling system
│   │   ├── framework_audio.h/c         # Audio resource manager
│   │   ├── framework_log.h/c           # Logging system
│   │   └── framework.c                 # Framework initialization
│   └── device/
│       ├── framework_key.h/c           # Key/button driver
│       ├── framework_battery.h/c       # Battery management
│       ├── framework_led.h/c           # LED control
│       ├── framework_device_config.h/c # Device configuration (volume, SN, MQTT)
│       ├── framework_mqtt.h/c          # MQTT client
│       └── framework_network.h/c       # Network management
│
├── customers/                          # Customer-Specific Layer
│   └── template/
│       ├── customer_config.h           # Configuration overrides
│       └── src/
│           ├── customer_handlers.c     # Custom message handlers
│           └── customer_battery_config.c  # Battery configuration
│
├── components/                         # Third-party Components
│   ├── lwip/                          # TCP/IP stack
│   ├── mbedtls/                       # Crypto library
│   ├── net/
│   │   ├── pahomqtt/                  # MQTT client
│   │   └── json/cJSON/                # JSON parser
│   └── wifi/                          # WiFi driver
│
└── examples/
    └── soundbox.c                     # Main application entry point
```

---

## Module Overview

### 1. Message System (`framework_msg`)

Centralized message queue for inter-module communication.

**Message Categories:**
- `0x1xxx`: Key events (press, release, long press)
- `0x2xxx`: Charging events (plugin, plugout, full, low battery)
- `0x3xxx`: Audio events (playback status)
- `0x4xxx`: Network events (connected, disconnected, got IP)
- `0x5xxx`: MQTT events (connected, disconnected, publish, receive)

**Key Functions:**
```c
int framework_msg_init(void);
int framework_msg_send(framework_msg_t *msg);
int framework_msg_register_callback(msg_process_cb_t callback);
```

**Usage Example:**
```c
framework_msg_t msg = {
    .id = MSG_KEY_VOL_UP,
    .param1 = KEY_STATE_DOWN,
    .param2 = 0
};
framework_msg_send(&msg);
```

### 2. Key Management (`framework_key`)

GPIO-based key detection with debouncing and long press support.

**Features:**
- Anti-shake (debounce) with configurable delay
- Long press detection
- Power key handling with VBUS filtering
- Interrupt-driven with work queue processing

**Key Functions:**
```c
int framework_key_init(void);
int framework_key_enable(key_id_e key_id, uint8_t enable);
int framework_key_register_pwk_callback(power_key_callback_t callback, void *param);
```

**Configuration:**
```c
/* In customer_config.h */
#define KEY_DOWN_PIN        55
#define KEY_UP_PIN          56
#define KEY_FUN_PIN         57
#define ANTI_SHAKE_MS       50
#define LONG_PRESS_TIMEOUT_MS  2000
```

### 3. Battery Management (`framework_battery`)

ADC-based battery monitoring with voltage-to-percentage conversion.

**Features:**
- Periodic battery level monitoring
- Configurable voltage thresholds
- Low battery warning
- Charging detection
- Critical battery shutdown

**Key Functions:**
```c
int framework_battery_init(void);
uint8_t framework_battery_get_level(void);
uint16_t framework_battery_get_voltage(void);
bool framework_battery_is_charging(void);
```

**Configuration:**
```c
/* Voltage to percentage mapping */
const battery_voltage_map_t CUSTOMER_BATTERY_VOLTAGE_MAP[] = {
    {4200, 100},  // 4.2V = 100%
    {3300, 0},    // 3.3V = 0%
};
```

### 4. LED Control (`framework_led`)

Multi-color LED status indication with pattern support.

**LED States:**
```c
LED_STATE_OFF              // All LEDs off
LED_STATE_BOOTING         // Device booting
LED_STATE_READY           // Ready for operation
LED_STATE_CHARGING        // Battery charging
LED_STATE_CHARGE_FULL     // Charge complete
LED_STATE_EXCEPTION_POWER // Low battery
LED_STATE_NET_CONNECTING  // Connecting to network
LED_STATE_NET_CONNECTED   // Network connected
```

**Key Functions:**
```c
int framework_led_init(void);
void framework_led_set_state(led_state_t state);
void framework_led_clear_state(led_state_t state);
```

### 5. Audio Resource Manager (`framework_audio`)

Multi-language audio resource management with priority queues.

**Features:**
- Three-priority queue system (high, medium, low)
- Multi-language support (Vietnamese, English)
- Automatic PA/Codec control
- Playback status callbacks
- MP3 format support

**Key Functions:**
```c
int audio_resource_manager_init(const audio_resource_config_t *config);
int audio_resource_play(audio_resource_id_t res_id, audio_playback_callback_t callback);
int audio_resource_play_high_priority(audio_resource_id_t res_id, audio_playback_callback_t callback);
int audio_resource_stop(void);
bool audio_resource_is_playing(void);
```

**Audio Resources:**
```c
AUDIO_RES_BOOT              // System boot
AUDIO_RES_SHUTDOWN          // System shutdown
AUDIO_RES_VOL_UP/DOWN       // Volume adjustment
AUDIO_RES_BAT_LOW           // Low battery warning
AUDIO_RES_CHG_CONNECTED     // Charger connected
AUDIO_RES_NET_CONNECTED     // Network connected
```

**Language Structure:**
```
D:/
├── VN/                    # Vietnamese
│   ├── boot.mp3
│   ├── vol_up.mp3
│   └── ...
└── EN/                    # English
    ├── boot.mp3
    ├── vol_up.mp3
    └── ...
```

### 6. Device Configuration (`framework_device_config`)

Persistent storage for device settings with backup support.

**Storage Strategy:**
- **Volume Configuration**: File storage (`D:/volume.cfg`)
  - Volume level, language setting
- **MQTT Configuration**: Flash partition (`user_cfg`)
  - Device SN, name, Product Key, Device Secret, Client ID

**Features:**
- Backup and restore mechanism
- Data validation with magic codes
- Delayed write to avoid audio interruption
- Background save thread

**Key Functions:**
```c
int framework_device_config_init(void);

// Volume
uint8_t framework_device_config_get_volume(void);
int framework_device_config_set_volume(uint8_t volume);

// Device Info
int framework_device_config_get_sn(char *sn, uint32_t max_len);
int framework_device_config_set_sn(const char *sn);

// MQTT Triple
int framework_device_config_get_product_key(char *product_key, uint32_t max_len);
int framework_device_config_set_product_key(const char *product_key);
int framework_device_config_get_device_secret(char *device_secret, uint32_t max_len);
int framework_device_config_set_device_secret(const char *device_secret);
```

### 7. MQTT Client (`framework_mqtt`)

MQTT client with auto-reconnection and message handling.

**Features:**
- Automatic connection management
- TLS/SSL support (via mbedTLS)
- QoS 0/1/2 support
- Keep-alive mechanism
- Topic subscription management
- JSON message parsing

**Key Functions:**
```c
int framework_mqtt_init(const framework_mqtt_config_t *config);
int framework_mqtt_connect(void);
int framework_mqtt_publish(const char *topic, const char *payload, int qos);
int framework_mqtt_subscribe(const char *topic, int qos, mqtt_message_callback_t callback);
```

### 8. Network Management (`framework_network`)

WiFi connection and IP configuration.

**Features:**
- WiFi station mode
- Auto-reconnection
- IP address management
- Network status monitoring

**Key Functions:**
```c
int framework_network_init(void);
int framework_network_connect(const char *ssid, const char *password);
int framework_network_disconnect(void);
bool framework_network_is_connected(void);
```

---

## Customer Integration

### Configuration Override System

The framework uses `#ifndef` guards to allow customer overrides:

**Framework Default (`framework_config.h`):**
```c
#ifndef KEY_DOWN_PIN
#define KEY_DOWN_PIN    55
#endif

#ifndef VOL_MAX
#define VOL_MAX         8
#endif
```

**Customer Override (`customer_config.h`):**
```c
#define KEY_DOWN_PIN    60    // Override with custom GPIO
#define VOL_MAX         10    // Override max volume
```

### Message Handler Callback

Customers can implement custom message handlers:

```c
int customer_msg_handler(framework_msg_t *msg)
{
    switch (msg->id) {
    case MSG_KEY_FUNCTION:
        // Custom function key handling
        OSI_LOGI(0, "Custom function key handler");
        audio_resource_play(AUDIO_RES_CUSTOM_BEEP, NULL);
        return 0;  // Message handled
        
    default:
        return -1; // Use framework default
    }
}

void customer_init(void)
{
    framework_msg_register_callback(customer_msg_handler);
}
```

### Audio Resource Mapping

Audio resources are defined in CSV files and auto-generated into C headers.

**✨ Automatic Generation (Recommended):**

Simply edit the CSV and build - CMake automatically regenerates the header when CSV is modified:

```bash
# 1. Edit CSV
notepad customers/template/audio_resources.csv

# 2. Build - auto-generation happens!
.\build.bat template

# CMake detects CSV changes:
# [1/X] Generating audio resource map for template from CSV
```

**CSV Format** (`customers/<name>/audio_resources.csv`):
```csv
RESOURCE_ID,filename,description
AUDIO_RES_SYS_STARTUP,10.mp3,System startup (欢迎使用)
AUDIO_RES_SYS_SHUTDOWN,11.mp3,System shutdown (正在关机)
AUDIO_RES_NET_CONNECTED,22.mp3,Network connected (联网成功)
```

**Manual Generation (Optional):**

If you want to generate without building:
```bash
# Windows
cd customers\template
generate_audio_map.bat

# Linux/Mac
cd customers/template
./generate_audio_map.sh
```

**Generated Header** (`customers/<name>/audio_resource_map.h`):
```c
/* Auto-generated - DO NOT EDIT MANUALLY */
static const audio_resource_map_entry_t CUSTOMER_AUDIO_RESOURCE_MAP[] = {
    /* System startup (欢迎使用) */
    {AUDIO_RES_SYS_STARTUP, "10.mp3"},
    /* System shutdown (正在关机) */
    {AUDIO_RES_SYS_SHUTDOWN, "11.mp3"},
    // ... more entries
};
#define CUSTOMER_AUDIO_RESOURCE_COUNT 48
```

**See `docs/AUDIO_RESOURCE_GENERATION.md` for complete generation guide.**

---

## Build System

### Building for a Customer

```bash
# Set customer name
export CUSTOMER_NAME=template  # Linux/Mac
set CUSTOMER_NAME=template     # Windows

# Build
cd Code/sdk
./build.sh                    # Linux/Mac
build.bat                     # Windows
```

### CMake Configuration

The build system automatically:
1. Includes customer configuration headers
2. Compiles customer source files
3. Links customer-specific libraries
4. Generates customer-specific binary

**CMakeLists.txt:**
```cmake
# Customer configuration
set(CUSTOMER_NAME $ENV{CUSTOMER_NAME})

# Include paths
include_directories(
    framework/inc
    framework/common
    framework/device
    customers/${CUSTOMER_NAME}
)

# Customer sources
file(GLOB CUSTOMER_SOURCES customers/${CUSTOMER_NAME}/src/*.c)

# Build target
add_appimg(customer_app ${flash_ldscript}
    examples/soundbox.c
    ${FRAMEWORK_SOURCES}
    ${CUSTOMER_SOURCES}
)
```

---

## API Reference

### Initialization Sequence

```c
int main(void)
{
    // 1. Initialize framework
    framework_init();
    
    // 2. Initialize customer modules
    customer_init();
    
    // 3. Start RTOS scheduler
    osiThreadSleep(UINT32_MAX);
    
    return 0;
}
```

### Framework Initialization

```c
int framework_init(void)
{
    framework_msg_init();              // Message system
    framework_device_config_init();    // Device configuration
    framework_key_init();              // Key management
    framework_battery_init();          // Battery monitoring
    framework_led_init();              // LED control
    audio_resource_manager_init(NULL); // Audio manager
    
    return 0;
}
```

---

## Configuration Reference

### GPIO Configuration

```c
/* Keys */
#define KEY_DOWN_PIN        55
#define KEY_UP_PIN          56
#define KEY_FUN_PIN         57

/* LEDs */
#define GPIO_LED_RED        10
#define GPIO_LED_GREEN      11
#define GPIO_LED_BLUE       12

/* Audio Hardware */
#define GPIO_PA             25    // Power Amplifier
#define GPIO_CODEC          26    // Codec
```

### Timing Configuration

```c
/* Key Timing */
#define ANTI_SHAKE_MS           50    // Debounce delay
#define LONG_PRESS_TIMEOUT_MS   2000  // Long press threshold
#define POWER_KEY_LONGPRESS_MS  3000  // Power off delay

/* Battery Monitoring */
#define BATTERY_CHECK_INTERVAL_MS  30000  // 30 seconds
```

### Volume Configuration

```c
#define VOL_MIN             1
#define VOL_MAX             8
#define VOL_STEP            1
#define DEFAULT_VOL         VOL_MIN
```

### Audio Configuration

```c
#define AUDIO_TASK_PRIORITY         150
#define AUDIO_QUEUE_HIGH_LEN        10
#define AUDIO_QUEUE_MID_LEN         20
#define AUDIO_QUEUE_LOW_LEN         30
```

---

## Debugging

### Log Levels

```c
OSI_LOGE(0, "Error message");     // Error
OSI_LOGW(0, "Warning message");   // Warning
OSI_LOGI(0, "Info message");      // Info
OSI_LOGD(0, "Debug message");     // Debug (disabled in release)
```

### Common Debug Scenarios

**Check message flow:**
```c
OSI_LOGI(0, "Received msg: id=0x%x, param1=%d, param2=%d",
         msg.id, msg.param1, msg.param2);
```

**Monitor battery:**
```c
uint8_t level = framework_battery_get_level();
uint16_t voltage = framework_battery_get_voltage();
OSI_LOGI(0, "Battery: %d%%, %dmV", level, voltage);
```

**Track audio playback:**
```c
void audio_callback(audio_resource_id_t res_id, 
                   audio_playback_status_t status, int result)
{
    OSI_LOGI(0, "Audio res=%d, status=%d, result=%d", 
             res_id, status, result);
}
```

---

## Best Practices

1. **Configuration Management**
   - Keep customer_config.h minimal
   - Document all overrides
   - Use meaningful macro names

2. **Resource Management**
   - Optimize audio file sizes
   - Limit memory allocations
   - Use static allocation where possible

3. **Error Handling**
   - Check return values
   - Log errors with context
   - Implement recovery mechanisms

4. **Testing**
   - Test all key combinations
   - Verify battery behavior at different levels
   - Test network connectivity edge cases
   - Validate audio playback priorities

---

## For More Information

### Documentation

- **Customer Integration**: `CUSTOMER_INTEGRATION_GUIDE.md` - How to add new customers
- **Audio Resource Generation**: `docs/AUDIO_RESOURCE_GENERATION.md` - CSV-to-header workflow
- **Audio CSV Quick Reference**: `docs/AUDIO_CSV_QUICKREF.md` - Quick command reference
- **Audio Resources Guide**: `docs/AUDIO_RESOURCES_GUIDE.md` - Audio system architecture
- **Framework Quick Reference**: `QUICKREF_Framework.md` - API quick reference

### Example Implementations

- **template Customer**: `customers/template/` - Complete reference implementation
- **template README**: `customers/template/README.md` - Customer-specific documentation

---

**SDK Version:** 1.0.0  
**Last Updated:** 2025-11-24  
**License:** Proprietary
