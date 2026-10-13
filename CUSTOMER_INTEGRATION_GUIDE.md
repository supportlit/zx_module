# Customer Integration Guide

This guide explains how to add a new customer to the ZxInfoTek Soundbox SDK Framework.

## Table of Contents
- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Step-by-Step Integration](#step-by-step-integration)
- [Configuration Options](#configuration-options)
- [Build and Test](#build-and-test)
- [Troubleshooting](#troubleshooting)

---

## Overview

The ZxInfoTek Soundbox SDK uses a customer-based architecture that allows you to:
- Override framework defaults with customer-specific configurations
- Implement custom message handlers and business logic
- Add customer-specific audio resources and hardware configurations
- Maintain separate codebases for different customers without conflicts

Each customer has their own directory under `customers/` containing:
- Configuration overrides (`customer_config.h`)
- Custom implementations (`src/`)
- Customer-specific resources

---

## Prerequisites

Before adding a new customer, ensure you have:

1. **Development Environment Set Up**
   - Windows PC with PowerShell
   - ARM GCC toolchain installed
   - CMake 3.13 or higher
   - Python 3.x for build scripts

2. **SDK Knowledge**
   - Familiarity with framework architecture
   - Understanding of RTOS concepts
   - Basic C programming skills

3. **Customer Requirements**
   - Hardware GPIO pin mappings
   - Audio resource file locations
   - MQTT broker configuration (if applicable)
   - Battery monitoring parameters

---

## Step-by-Step Integration

### 1. Create Customer Directory Structure

Create a new customer directory with the required structure:

```
customers/
└── <customer_name>/
    ├── customer_config.h        # Configuration overrides
    ├── audio_resources.csv      # Audio resource definitions (source)
    ├── audio_resource_map.h     # Generated audio resource header
    ├── generate_audio_map.bat   # Windows generator script
    ├── generate_audio_map.sh    # Linux/Mac generator script
    └── src/
        ├── customer_handlers.c  # Custom message handlers
        └── customer_battery_config.c  # Battery configuration (optional)
```

**Example for customer "acme":**

```bash
cd Code/sdk/customers
mkdir acme
mkdir acme/src
```

### 2. Create Configuration Header

Create `customer_config.h` to override framework defaults:

```c
/**
 * @file customer_config.h
 * @brief ACME Customer Configuration Overrides
 */

#ifndef __CUSTOMER_CONFIG_H__
#define __CUSTOMER_CONFIG_H__

/* Customer Identification */
#define CUSTOMER_NAME "ACME"

/* ==================== GPIO Configuration ==================== */

/* Key GPIO Pins */
#define KEY_DOWN_PIN        55    /* Volume down button */
#define KEY_UP_PIN          56    /* Volume up button */
#define KEY_FUN_PIN         57    /* Function button */

/* Key GPIO PINMUX Configuration */
#define KEY_DOWN_PINMUX     55
#define KEY_UP_PINMUX       56
#define KEY_FUN_PINMUX      57

/* Key GPIO Function Selection */
#define KEY_DOWN_FUN        MFP_AF0
#define KEY_UP_FUN          MFP_AF0
#define KEY_FUN_FUN         MFP_AF0

/* LED GPIO Pins */
#define GPIO_LED_RED        10
#define GPIO_LED_GREEN      11
#define GPIO_LED_BLUE       12

/* Audio Hardware GPIO */
#define GPIO_PA             25    /* Power Amplifier enable */
#define GPIO_CODEC          26    /* Codec enable */

/* PA/Codec PINMUX */
#define PA_PINMUX           25
#define CODEC_PINMUX        26
#define PA_PINFUN           MFP_AF0
#define CODEC_PINFUN        MFP_AF0

/* ==================== Timing Configuration ==================== */

/* Key Press Timing (milliseconds) */
#define ANTI_SHAKE_MS           50    /* Debounce delay */
#define LONG_PRESS_TIMEOUT_MS   2000  /* Long press threshold */
#define POWER_KEY_LONGPRESS_MS  3000  /* Power key shutdown delay */

/* Battery Monitoring */
#define BATTERY_CHECK_INTERVAL_MS    30000   /* 30 seconds */
#define LOW_BATTERY_THRESHOLD_MV     3300    /* 3.3V */
#define CRITICAL_BATTERY_THRESHOLD_MV 3100   /* 3.1V */

/* ==================== Volume Configuration ==================== */

#define VOL_MIN             1     /* Minimum volume level */
#define VOL_MAX             8     /* Maximum volume level */
#define VOL_STEP            1     /* Volume increment step */
#define DEFAULT_VOL         VOL_MIN

/* ==================== Audio Configuration ==================== */

/* Audio Resource Paths */
#define AUDIO_LANG_VIETNAMESE_PATH  "D:/VN/"
#define AUDIO_LANG_ENGLISH_PATH     "D:/EN/"

/* Audio Task Priority */
#define AUDIO_TASK_PRIORITY         150

/* ==================== Network Configuration ==================== */

/* WiFi Configuration */
#define WIFI_ENABLED                1
#define WIFI_DEFAULT_SSID           "ACME_SOUNDBOX"
#define WIFI_DEFAULT_PASSWORD       ""

/* MQTT Configuration */
#define MQTT_ENABLED                1
#define MQTT_BROKER_HOST            "mqtt.acme.com"
#define MQTT_BROKER_PORT            1883
#define MQTT_CLIENT_ID_PREFIX       "ACME_SB_"
#define MQTT_KEEPALIVE_INTERVAL     60

/* ==================== Device Information ==================== */

#define DEVICE_MANUFACTURER         "ACME Corporation"
#define DEVICE_MODEL                "ACME-SB-1000"
#define FIRMWARE_VERSION            "1.0.0"

/* File Paths */
#define DEVICE_INFO_PATH            "D:/acme_device.cfg"
#define DEVICE_SN_PATH              "D:/acme_sn.cfg"
#define WIFI_CONFIG_PATH            "D:/acme_wifi.cfg"

#endif /* __CUSTOMER_CONFIG_H__ */
```

### 3. Implement Customer Handlers

Create `src/customer_handlers.c` for custom business logic:

```c
/**
 * @file customer_handlers.c
 * @brief ACME Customer Message Handlers
 */

#include "framework.h"
#include "customer_config.h"

/**
 * @brief Custom message handler
 * @details Called before framework default handlers
 * @return 0 if message handled, -1 to use default handler
 */
int customer_msg_handler(framework_msg_t *msg)
{
    if (msg == NULL) {
        return -1;
    }

    switch (msg->id) {
    case MSG_KEY_FUNCTION:
        if (msg->param1 == KEY_STATE_DOWN) {
            OSI_LOGI(0, "ACME: Function key pressed");
            /* Custom function key handling */
            audio_resource_play(AUDIO_RES_KEY_BEEP, NULL);
            return 0;  /* Message handled */
        }
        break;

    case MSG_CHARGE_PLUGIN:
        OSI_LOGI(0, "ACME: Charger connected");
        /* Custom charging logic */
        audio_resource_play(AUDIO_RES_CHG_CONNECTED, NULL);
        return 0;
        break;

    default:
        break;
    }

    return -1;  /* Use framework default handler */
}

/**
 * @brief Customer initialization function
 */
void customer_init(void)
{
    OSI_LOGI(0, "ACME: Initializing customer-specific modules");

    /* Register custom message handler */
    framework_msg_register_callback(customer_msg_handler);

    /* Initialize custom modules here */
    // customer_mqtt_init();
    // customer_payment_init();

    OSI_LOGI(0, "ACME: Initialization complete");
}
```

### 4. Create Audio Resource Configuration

Audio resources are defined using CSV files that are auto-generated into C headers by the build system.

#### Step 4.1: Create CSV Configuration

Create `audio_resources.csv`:

```csv
RESOURCE_ID,filename,description
AUDIO_RES_SYS_STARTUP,boot.mp3,System startup
AUDIO_RES_SYS_SHUTDOWN,shutdown.mp3,System shutdown
AUDIO_RES_KEY_BEEP,beep.mp3,Key feedback beep
AUDIO_RES_VOL_UP,vol_up.mp3,Volume up
AUDIO_RES_VOL_DOWN,vol_down.mp3,Volume down
AUDIO_RES_VOL_MAX,vol_max.mp3,Volume maximum
AUDIO_RES_VOL_MIN,vol_min.mp3,Volume minimum
AUDIO_RES_BAT_LOW,battery_low.mp3,Battery low warning
AUDIO_RES_BAT_CHARGE_COMPLETE,charge_complete.mp3,Charge complete
AUDIO_RES_CHG_CONNECTED,charge_connected.mp3,Charger connected
AUDIO_RES_CHG_DISCONNECTED,charge_disconnected.mp3,Charger disconnected
AUDIO_RES_NET_CONNECTED,net_connected.mp3,Network connected
AUDIO_RES_NET_DISCONNECTED,net_disconnected.mp3,Network disconnected
```

#### Step 4.2: Build with Auto-Generation

**✨ NEW: CMake automatically generates the header when you build!**

```bash
# Just build - CMake detects CSV and generates header automatically
cd Code/sdk
.\build.bat acme

# You'll see:
# -- Audio resource auto-generation enabled for acme
# [1/X] Generating audio resource map for acme from CSV
# Generated 13 audio resource mappings for customer 'acme'
```

The build system automatically:
- Detects `audio_resources.csv` exists
- Checks if CSV is newer than header
- Runs generator when needed
- Creates `audio_resource_map.h`

#### Step 4.3: Manual Generation (Optional)

If you want to generate the header without building, copy the generator scripts:

```bash
# Copy scripts from template customer
# Windows
copy customers\template\generate_audio_map.bat customers\acme\
copy customers\template\generate_audio_map.sh customers\acme\

# Linux/Mac
cp customers/template/generate_audio_map.* customers/acme/
```

Update the `CUSTOMER_NAME` variable in both scripts from `template` to `acme`, then run:

```bash
# Windows
cd customers\acme
generate_audio_map.bat

# Linux/Mac
cd customers/acme
./generate_audio_map.sh
```

**Generated Output:**

```c
/* Auto-generated audio resource map for acme customer */
/* DO NOT EDIT THIS FILE MANUALLY */

#ifndef CUSTOMER_ACME_AUDIO_RESOURCE_MAP_H
#define CUSTOMER_ACME_AUDIO_RESOURCE_MAP_H

typedef struct {
    audio_resource_id_t id;
    const char *filename;
} audio_resource_map_entry_t;

static const audio_resource_map_entry_t CUSTOMER_AUDIO_RESOURCE_MAP[] = {
    /* System startup */
    {AUDIO_RES_SYS_STARTUP, "boot.mp3"},
    /* System shutdown */
    {AUDIO_RES_SYS_SHUTDOWN, "shutdown.mp3"},
    /* ... more entries ... */
};

#define CUSTOMER_AUDIO_RESOURCE_COUNT 13

#endif
```

**Important:** 
- ✨ **CMake auto-generates** when CSV is modified - no manual step needed
- Always edit the CSV file, not the generated header
- Build system detects changes automatically
- Manual scripts are optional (for pre-generation)
- See `docs/AUDIO_RESOURCE_GENERATION.md` for complete details

### 5. Create Battery Configuration (Optional)

For custom battery monitoring, create `src/customer_battery_config.c`:

```c
/**
 * @file customer_battery_config.c
 * @brief ACME Battery Configuration
 */

#include "framework_battery.h"

/* Battery voltage to percentage mapping */
const battery_voltage_map_t CUSTOMER_BATTERY_VOLTAGE_MAP[] = {
    {4200, 100},  /* 4.2V = 100% */
    {4100, 95},
    {4000, 85},
    {3900, 75},
    {3800, 60},
    {3700, 45},
    {3600, 30},
    {3500, 15},
    {3400, 5},
    {3300, 0},    /* 3.3V = 0% */
};

const uint32_t CUSTOMER_BATTERY_MAP_SIZE = 
    sizeof(CUSTOMER_BATTERY_VOLTAGE_MAP) / sizeof(battery_voltage_map_t);

/* Battery configuration */
const battery_config_t CUSTOMER_BATTERY_CONFIG = {
    .check_interval_ms = 30000,          /* Check every 30 seconds */
    .low_battery_threshold = 3300,       /* 3.3V warning */
    .critical_battery_threshold = 3100,  /* 3.1V critical */
    .full_battery_voltage = 4200,        /* 4.2V full */
    .enable_percentage_calc = true,
    .enable_charging_detection = true,
};
```

### 6. Update Build Configuration

Modify the build script to include your customer:

**Edit `Code/sdk/build.bat`:**

```batch
@echo off
set CUSTOMER_NAME=acme
set MODULE_VERSION=ZX800VG_SA

cmake -G "Ninja" ^
    -DCUSTOMER_NAME=%CUSTOMER_NAME% ^
    -B build_%CUSTOMER_NAME%

cmake --build build_%CUSTOMER_NAME%
```

**Or use the environment variable:**

```bash
# Linux/Mac
export CUSTOMER_NAME=acme
./build.sh

# Windows
set CUSTOMER_NAME=acme
build.bat
```

### 7. Verify Directory Structure

Your customer directory should now look like this:

```
customers/acme/
├── customer_config.h
└── src/
    ├── customer_handlers.c
    ├── customer_battery_config.c
    └── audio_resource_map.c
```

---

## Configuration Options

### GPIO Pin Configuration

The framework uses the following GPIO categories:

| Category | Purpose | Configuration Required |
|----------|---------|------------------------|
| Keys | Physical buttons | `KEY_*_PIN`, `KEY_*_PINMUX` |
| LEDs | Status indicators | `GPIO_LED_*` |
| Audio | PA and Codec control | `GPIO_PA`, `GPIO_CODEC` |
| Battery | ADC monitoring | Handled by framework |
| Power | Power key detection | Hardware-specific |

### Timing Parameters

Adjust timing parameters based on hardware characteristics:

```c
/* Debounce timing - adjust for button quality */
#define ANTI_SHAKE_MS           50    /* 20-100ms typical */

/* Long press detection */
#define LONG_PRESS_TIMEOUT_MS   2000  /* 1000-3000ms */

/* Power key shutdown delay */
#define POWER_KEY_LONGPRESS_MS  3000  /* 3000-5000ms */
```

### Audio Configuration

Configure audio paths and priorities:

```c
/* Language folders */
#define AUDIO_LANG_VIETNAMESE_PATH  "D:/VN/"
#define AUDIO_LANG_ENGLISH_PATH     "D:/EN/"

/* Task priority (lower number = higher priority) */
#define AUDIO_TASK_PRIORITY         150  /* 100-200 typical */
```

### Battery Monitoring

Customize battery behavior:

```c
/* Monitoring interval */
#define BATTERY_CHECK_INTERVAL_MS    30000   /* 10-60 seconds */

/* Voltage thresholds (millivolts) */
#define LOW_BATTERY_THRESHOLD_MV     3300    /* Warning level */
#define CRITICAL_BATTERY_THRESHOLD_MV 3100   /* Shutdown level */
```

---

## Build and Test

### 1. Build the Firmware

```bash
cd Code/sdk
set CUSTOMER_NAME=acme
build.bat
```

Expected output:
```
Building for customer: acme
Configuring CMake...
Building firmware...
Creating app image...
Build completed successfully!
Output: build_acme/hex/customer_app.bin
```

### 2. Flash the Device

```bash
download.bat -app
```

### 3. Verify Functionality

Check the following features:

**Basic Functions:**
- [ ] Device boots successfully
- [ ] LEDs indicate correct status
- [ ] Keys respond to presses
- [ ] Audio prompts play correctly
- [ ] Volume control works

**Battery Management:**
- [ ] Battery percentage displays correctly
- [ ] Low battery warning triggers
- [ ] Charging detection works
- [ ] Charge complete notification

**Network (if enabled):**
- [ ] WiFi connects successfully
- [ ] MQTT connection established
- [ ] Messages send/receive correctly

### 4. Debug Output

Monitor debug logs via serial console:

```
[framework] Initializing all modules
[acme] Initializing customer-specific modules
[device_config] Initialized - vol=1, lang=0, sn=ACME001
[audio] Language set to 0
[battery] Battery level: 85%, voltage: 3850mV
[acme] Initialization complete
```

---

## Troubleshooting

### Build Errors

**Problem: "customer_config.h not found"**
```
Solution: Ensure customer directory exists in customers/
Check: customers/<customer_name>/customer_config.h
```

**Problem: "Undefined reference to customer functions"**
```
Solution: Add .c files to CMakeLists.txt CUSTOMER_SOURCES
Check: file(GLOB CUSTOMER_SOURCES customers/${CUSTOMER_NAME}/src/*.c)
```

### Runtime Issues

**Problem: Audio files not playing**
```
Debug steps:
1. Check audio file paths in audio_resource_map.c
2. Verify files exist on device (D:/VN/, D:/EN/)
3. Check file format (MP3, correct encoding)
4. Monitor logs for "File not found" errors
```

**Problem: GPIO not responding**
```
Debug steps:
1. Verify pin numbers match hardware schematic
2. Check PINMUX configuration (MFP_AF0, MFP_AF1, etc.)
3. Test with simple GPIO toggle
4. Verify pull-up/pull-down settings
```

**Problem: Battery percentage incorrect**
```
Debug steps:
1. Verify voltage mapping in customer_battery_config.c
2. Check ADC calibration values
3. Test with known battery voltage
4. Adjust thresholds if needed
```

### Common Mistakes

1. **Forgetting to set CUSTOMER_NAME environment variable**
   - Always set before building: `set CUSTOMER_NAME=acme`

2. **Using absolute paths in configuration**
   - Use relative paths: `"D:/VN/"` not `"C:/Users/..."`

3. **Not implementing required callbacks**
   - All handler functions must be implemented or use weak stubs

4. **Incorrect GPIO configuration**
   - Double-check hardware schematic
   - Verify PINMUX values match chip datasheet

5. **Audio resource path mismatch**
   - Ensure folder structure matches configuration
   - Check file extensions (.mp3 not .MP3)

---

## Advanced Topics

### Multi-Customer Support

To support multiple customers in the same workspace:

```bash
# Build for customer A
set CUSTOMER_NAME=customer_a
build.bat

# Build for customer B
set CUSTOMER_NAME=customer_b
build.bat
```

Each build creates separate output:
- `build_customer_a/hex/customer_app.bin`
- `build_customer_b/hex/customer_app.bin`

### Conditional Compilation

Use preprocessor directives for customer-specific code:

```c
#ifdef CUSTOMER_ACME
    /* ACME-specific code */
#elif defined(CUSTOMER_TEMPLATE)
    /* ZxInfoTek-specific code */
#else
    /* Default code */
#endif
```

### Customer-Specific Libraries

To link customer-specific libraries:

1. Place library files in `customers/<name>/lib/`
2. Update `CMakeLists.txt`:

```cmake
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/customers/${CUSTOMER_NAME}/lib)
    link_directories(customers/${CUSTOMER_NAME}/lib)
    target_link_libraries(${target} PRIVATE customer_lib)
endif()
```

---

## Best Practices

1. **Configuration Management**
   - Keep customer_config.h concise
   - Document all overrides
   - Use meaningful macro names

2. **Code Organization**
   - Separate business logic from hardware control
   - Use framework APIs instead of direct hardware access
   - Keep customer code minimal

3. **Resource Management**
   - Optimize audio file sizes
   - Limit memory usage in handlers
   - Use static allocation where possible
   - Always regenerate audio_resource_map.h after CSV changes

4. **Audio Resource Workflow**
   - Edit CSV file for audio changes
   - Run generator script before building
   - Never manually edit generated headers
   - See `docs/AUDIO_RESOURCE_GENERATION.md` for details

5. **Testing**
   - Test all key combinations
   - Verify battery behavior at different levels
   - Test edge cases (rapid key presses, etc.)
   - Validate audio playback for all resources

6. **Documentation**
   - Document custom configurations
   - Add comments explaining hardware-specific settings
   - Maintain change log for customer code
   - Keep CSV descriptions up-to-date

---

## Support

For additional help:

1. **Framework Documentation**: 
   - Main framework: `README.md`
   - Audio generation: `docs/AUDIO_RESOURCE_GENERATION.md`
   - Audio resources: `docs/AUDIO_RESOURCES_GUIDE.md`

2. **Reference Implementation**: Review `customers/template/` for complete example

3. **Hardware Documentation**: Consult datasheets for GPIO/PINMUX values

4. **Technical Support**: Contact SDK support team for integration assistance

---

## Appendix A: Complete Example

See `customers/template/` for a complete reference implementation including:
- Full configuration override
- Custom message handlers
- Battery configuration
- Audio resource CSV configuration and generated header
- Audio resource generator scripts
- MQTT integration
- Device configuration management

## Appendix B: Configuration Checklist

Before deploying:

- [ ] All GPIO pins configured correctly
- [ ] Audio files present on device
- [ ] Battery thresholds set appropriately
- [ ] Network credentials configured (if applicable)
- [ ] Device SN/name set correctly
- [ ] All features tested
- [ ] Debug logs reviewed
- [ ] Production build tested

---

**Document Version:** 1.0  
**Last Updated:** 2025-11-24  
**Applicable SDK Version:** 1.0.0+
