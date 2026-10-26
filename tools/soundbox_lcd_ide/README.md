# SoundBox LCD Preview IDE — Hardware & Screen Studio

A Python desktop IDE for embedded SoundBox terminal UI development. Allows firmware engineers and designers to preview text labels, fonts, 7-segment character formations, alignments, physical buttons, screen layouts, and payment broadcasts **without flashing the physical device or transmitting real network/payment traffic**.

---

## 1. Quick Launch (Windows)

No external libraries (such as Pillow or PyQt) are required. The IDE runs directly on the standard Python 3 runtime with Tkinter.

### Launch Command:
```cmd
cd e:\LIT\SoundBox\Zx\Soundbox_OPEN_SDK_ZxInfoTek_V2-main\Code\zx_module\tools\soundbox_lcd_ide
python main.py
```

### Run Automated Unit Tests:
```cmd
cd e:\LIT\SoundBox\Zx\Soundbox_OPEN_SDK_ZxInfoTek_V2-main\Code\zx_module
python -m unittest discover -s tools/soundbox_lcd_ide/tests -p "test_*.py"
```

---

## 2. Firmware Architecture & Hardware Grounding

The IDE's simulation engine is directly mapped to the C source code of the SoundBox SDK:

| Subsystem | Real Source Location | Hardware Implementation in Firmware | Emulated Behavior in IDE |
|---|---|---|---|
| **LCD Controller** | `customers/template/src/app_lcd.c` | Titan Micro Electronics **TM1726** (1/8 Duty, 1/4 Bias LCD driver) | Emulates 18-byte display buffer `disp_buf[18]` and 3-wire SPI communication |
| **Main Digits** | `app_lcd.c:160-179` (`recv_data_map[8][7]`) | 8 large 7-segment digits (`A` through `G`) with decimal points on each | Exact segment-to-byte bitwise mapping across ROW 7 down to ROW 0 |
| **Count Digits** | `app_lcd.c:425-436` (`recv_data_map[4][7]`) | 4 smaller 7-segment digits (`D, C, B, A, E, G, F`) on the right | Real-time display of total daily transactions (`s_total_txn_count`) |
| **Hitachi Logo** | `app_lcd.c:1359-1403` (`hitachi_patterns[8]`) | Multi-segment layout: `H`, `I`, `T` left (0x07), `T` right (0x31), `A`, `C`, `H`, `I` | Exactly replicates double-digit `T` spanning digits 2 & 3 |
| **Brand Logo** | `app_lcd.c:362` (`lit_patterns[3]`) | Displays `L`, `I`, `t` across digits 0, 1, 2 | `appLcdShowTextLIT()` reproduction |
| **Status Icons** | `app_lcd.c:773-1080` | Dedicated LCD segment flags: `Hook` (✔), `Rs.` (₹), `COUNT`, `Paytm`, `MUTE` | Bitwise COM/ROW toggling on rows 10, 11, 13, 14 |
| **Signal Bars** | `app_lcd.c:959` (`signal_flag_map[5]`) | 5-level cellular network ladder | Toggled by `MSG_NET_CONNECTED`, `MSG_NET_DISCONNECTED` |
| **Battery Power** | `app_lcd.c:1007` (`power_flag_map[5]`) | 4 internal charge bars + battery shell | Toggled by `MSG_BATTERY_STATUS`, `appLcdenablecharger()` |
| **Volume Level** | `app_lcd.c:900` (`volume_flag_map[5]`) | 5-level speaker and sound wave indicator | Toggled by `MSG_KEY_UP` (pin 33), `MSG_KEY_DOWN` (pin 34) |
| **Graphic TFT** | `framework/common/framework_lcd9853.c` | 240x240 RGB565 ST7789 TFT display | Vector & bitmap font rendering with bounding boxes and line wraps |

---

## 3. Key IDE Features

### A. Dual Display Modes
1. **Physical SoundBox Segment LCD Mode**:
   - Authentic segment geometry with tapered polygon corners and unlit segment ghosting.
   - 4 High-contrast backlight color schemes:
     - **Amber Gold** (Default SoundBox look: `#FF9E00` on `#1A1200`)
     - **Emerald Green** (POS Terminal look: `#20EE44` on `#001A08`)
     - **Ice Blue** (OLED look: `#00D5FF` on `#001424`)
     - **Silver / Mono** (Transflective unlit LCD: `#111A11` on `#98A698`)
   - Zoom controls: 1.0x, 1.5x, 2.0x, 3.0x.
2. **Graphic Color TFT Mode (240x240 / 240x320)**:
   - Pixel rulers on top and left.
   - Configurable grid overlay.
   - Safe-area margin boundaries.
   - Red dashed overflow boundary warning when any element exceeds screen limits.

### B. Live Text & Segment Editor
- Type any string or amount to see it rendered instantly on the 7-segment display.
- **7-Segment Glyph Validator**:
  - Automatically identifies whether characters can be natively formed on a 7-segment display.
  - Warns when characters like `W`, `M`, `K`, `X`, or non-Latin scripts (e.g. Hindi/Devanagari) are used on physical 7-segment hardware.
  - Shows exact hex byte representations (e.g. `0x76` for `H`, `0x06` for `I`).
- **Graphic LCD Text Controls**:
  - Edit X, Y, font size, font family (5x7, 8x16, Arial, Courier), color.
  - Configure maximum width and maximum lines with automated word wrapping.
  - Live character counter and pixel measurement readout.

### C. Interactive Hardware Simulator & C Callback Trace
Simulates physical device inputs and inspects the resulting state changes in real time:
- **Vol+ Button (GPIO Pin 33)**: Triggers `customer_msg_handler() -> MSG_KEY_UP`, increments volume 1..8, updates volume bars.
- **Vol- Button (GPIO Pin 34)**: Triggers `customer_msg_handler() -> MSG_KEY_DOWN`, decrements volume with minimum boundary check at 1.
- **Function Key (Replay)**: Calls `customer_payment_replay_last()`, replays last transaction on LCD without incrementing daily transaction count.
- **Charger Plug-In (VBUS)**: Calls `appLcdenablecharger()`, resets battery icon for charging animation.
- **Network Events**: Simulates `MSG_NET_CONNECTED`, `MSG_NET_DISCONNECTED`, `MSG_NET_SIM_NOT_READY` (NO SIM lit), `MSG_NET_NO_SIGNAL`.
- **MQTT Payment Message**: Simulates arrival of payment payload (e.g. `EN|UPI|1590.00| | |415857764147`), lighting up the Success Checkmark (Hook), Rs. symbol, amount, and transaction count.
- **Idle Timeout (10s)**: Reverts screen to "HITACHI" while retaining daily transaction count.
- **TM1726 RAM Inspector**: Displays raw 18-byte hex dump and binary COM bits of `disp_buf[18]`.

### D. Automated Text Stress-Testing Suite
- Pre-loaded with 18 real-world test cases:
  - Standard amounts (`1590.00`, `250.50`)
  - Boundary limits (`0.01`, `99999999`)
  - 8-digit overflow tests (`100000000`)
  - 7-segment words (`HITACHI`, `LIT`, `ERROR`, `HELP`, `bAt-LO`)
  - Unsupported character checks (`WIFI`, `KM-MAX`)
  - Unicode & Hindi script checks (`₹ १५९०.००`, `भुगतान सफल हुआ`)
- Produces pass/warn/fail status with detailed metrics and allows exporting markdown reports.

### E. Project Persistence & C Code Export
- **JSON Project File**: Save and reload preview configurations, element coordinates, and register states.
- **PNG & BMP Export**: Exports preview images using a custom, pure-Python PNG and BMP encoder (zero third-party dependencies required).
- **C Header Stub Generator**: Generates C code with the exact 18-byte array ready to paste into `customers/template/src/app_lcd.c`.

---

## 4. Verification & Fidelity Report

| Feature / Behavior | Classification | Technical Evidence & Details |
|---|---|---|
| **7-Segment Digit Patterns (0-9)** | **EXACT** | Matches `seg_map` in `app_lcd.c:88-99`. |
| **"HITACHI" Multi-Segment Pattern** | **EXACT** | Matches `hitachi_patterns[8]` in `app_lcd.c:1394-1403`. |
| **"LIT" Pattern** | **EXACT** | Matches `lit_patterns[3]` in `app_lcd.c:362`. |
| **TM1726 Memory Map & Pinout** | **EXACT** | Replicates `recv_data_map[8][7]`, `small_digits_map[4][7]`, and `point_flag_map[8]`. |
| **Status Indicators (Hook, Rs, Count, Paytm, Mute)** | **EXACT** | Mapped to exact ROW and COM bits in `disp_buf[18]`. |
| **Payment Display & 10s Idle Timeout** | **EXACT** | Mapped to `customer_payment_broadcast()` and `s_lcd_idle_timer` in `customer_payment_handler.c:508-535`. |
| **Volume Step Clamping** | **EXACT** | Enforces minimum volume 1 and maximum volume 8 from `customer_config.h:51-52`. |
| **Graphic Font Dimensions (5x7, 8x16)** | **EXACT** | Matches `DISP_ASCII` and `DISP_CFONT` in `framework_lcd9853.h`. |
| **LCD Backlight Optical Response & Viewing Angle** | **APPROXIMATED** | Emulated via 4 color palettes with unlit segment ghosting; physical optical contrast depends on ambient light and polarizers. |
| **Devanagari Font Rendering on Graphic TFT** | **UNVERIFIED (EXTERNAL)** | Requires inspecting the external font file `Charset_ascii_24_24.FON` on physical flash to verify Hindi font tables. |
