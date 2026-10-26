"""
Firmware LCD Adapter & Hardware Emulator
Simulates the exact C-level behavior of:
- customers/template/src/app_lcd.c
- customers/template/src/customer_handlers.c
- customers/template/src/customer_payment_handler.c

Maintains the exact 18-byte disp_buf array and reproduces register-level COM/ROW mappings.
"""

import time
import datetime
from typing import List, Dict, Tuple, Optional, Callable
from font_manager import (
    FIRMWARE_SEG_MAP_DIGITS,
    FIRMWARE_HITACHI_PATTERNS,
    HITACHI_PATTERNS_CLEAN_T,
    FIRMWARE_LIT_PATTERNS,
    encode_7segment_char
)

def COM_ROW(x: int, y: int) -> int:
    """Equivalent to C macro: ((((X) & 0xff) << 8) | ((Y) & 0xff))"""
    return (((x & 0xFF) << 8) | (y & 0xFF))

def COM(val: int) -> int:
    """Equivalent to C macro: (((X) >> 8) & 0xff)"""
    return (val >> 8) & 0xFF

def ROW(val: int) -> int:
    """Equivalent to C macro: ((Y) & 0xff)"""
    return val & 0xFF


class FirmwareLcdAdapter:
    """
    Exact emulation of the TM1726 controller and SoundBox firmware LCD driver.
    """

    def __init__(self):
        # 18-byte display buffer from app_lcd.c:85 (static char disp_buf[18] = {0};)
        self.disp_buf = [0] * 18
        
        # State variables from app_lcd.c & customer_payment_handler.c
        self.decimal = 0
        self.front_fill = 0
        self.volume_level = 1       # Default volume from customer_config.h (1..8)
        self.volume_bar_level = 1   # LCD bar indicator (0..4)
        self.signal_level = 4       # Default 4 bars
        self.power_level = 4        # Default 4 bars battery
        self.is_mute = False
        self.no_signal = False
        self.no_sim = False
        self.no_connect = False
        self.hook_en = False
        self.rs_en = False
        self.count_en = False
        self.paytm_en = False
        self.current_screen_name = "INIT"
        self.total_txn_count = 0
        self.last_payment_amount = 0.0
        
        # Rotating Idle Screen State Machine (Hitachi -> Time -> Date)
        self.is_payment_active = False
        self.rotating_idle_stage = 0  # 0: HITACHI, 1: TIME, 2: DATE
        
        # Event audit log for interactive debugging
        self.event_log: List[Dict] = []
        self.listeners: List[Callable[[], None]] = []

        # --------------------------------------------------------------------
        # Pin and Bit Mappings from app_lcd.c (PAYTM_NEW_LCD configuration)
        # --------------------------------------------------------------------

        # 8 Large Digits Map: recv_data_map[8][7] (app_lcd.c:160-179)
        # Order of segments: A, B, C, D, E, F, G
        self.large_digits_map = [
            [COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7), COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)],
            [COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6), COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)],
            [COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5), COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)],
            [COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4), COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)],
            [COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3), COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)],
            [COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2), COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)],
            [COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1), COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)],
            [COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0), COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)],
        ]

        # Decimal Points Map: point_flag_map[8] (app_lcd.c:107-109)
        self.point_flag_map = [
            COM_ROW(3, 7), COM_ROW(3, 6), COM_ROW(3, 5), COM_ROW(3, 4),
            COM_ROW(3, 3), COM_ROW(3, 2), COM_ROW(3, 1), COM_ROW(3, 0)
        ]

        # 4 Small Digits Map (Count / Amount): recv_data_map[4][7] (app_lcd.c:425-436)
        # Order of segments: D, C, B, A, E, G, F
        self.small_digits_map = [
            [COM_ROW(4, 8),  COM_ROW(1, 8),  COM_ROW(3, 8),  COM_ROW(7, 8),  COM_ROW(6, 8),  COM_ROW(5, 8),  COM_ROW(2, 8)],
            [COM_ROW(4, 9),  COM_ROW(1, 9),  COM_ROW(3, 9),  COM_ROW(7, 9),  COM_ROW(6, 9),  COM_ROW(5, 9),  COM_ROW(2, 9)],
            [COM_ROW(4, 10), COM_ROW(1, 10), COM_ROW(3, 10), COM_ROW(7, 10), COM_ROW(6, 10), COM_ROW(5, 10), COM_ROW(2, 10)],
            [COM_ROW(4, 11), COM_ROW(1, 11), COM_ROW(3, 11), COM_ROW(7, 11), COM_ROW(6, 11), COM_ROW(5, 11), COM_ROW(2, 11)],
        ]

        # Dedicated Status Indicators & Icons (app_lcd.c:773-1080)
        self.icon_rs = COM_ROW(0, 10)         # Rs. icon
        self.icon_hook = COM_ROW(2, 13)       # Success tick/hook
        self.icon_count = COM_ROW(0, 11)      # COUNT icon
        self.icon_paytm = COM_ROW(3, 13)      # PayTm logo icon
        self.icon_mute = [COM_ROW(0, 13), COM_ROW(1, 13)] # Mute symbol

        # Volume bars (5 levels) (app_lcd.c:900-901)
        self.volume_flag_map = [
            COM_ROW(4, 13), COM_ROW(5, 13), COM_ROW(6, 13), COM_ROW(7, 13), COM_ROW(3, 14)
        ]

        # Signal bars (5 levels) (app_lcd.c:959-960)
        self.signal_flag_map = [
            COM_ROW(3, 12), COM_ROW(2, 12), COM_ROW(1, 12), COM_ROW(0, 12), COM_ROW(7, 12)
        ]

        # Battery bars (5 levels) (app_lcd.c:1007-1008)
        self.power_flag_map = [
            COM_ROW(4, 14), COM_ROW(2, 14), COM_ROW(0, 14), COM_ROW(1, 14), COM_ROW(5, 14)
        ]

        # Error / Network icons (app_lcd.c:1046-1080)
        self.icon_nosignal = COM_ROW(6, 12)
        self.icon_nosim = COM_ROW(5, 12)
        self.icon_noconnect = COM_ROW(4, 12)

        # Boot into Hitachi idle screen matching real device photo (Day Count 5)
        self.total_txn_count = 5
        self.appLcdenable()
        self.appLcdShowTextHitachi()

    def add_listener(self, cb: Callable[[], None]):
        self.listeners.append(cb)

    def _notify(self):
        for cb in self.listeners:
            try:
                cb()
            except Exception:
                pass

    def log_event(self, event_id: str, handler: str, desc: str, details: Optional[Dict] = None):
        """Record an interactive event for the UI debugging panel."""
        entry = {
            'timestamp': time.strftime('%H:%M:%S'),
            'event_id': event_id,
            'handler': handler,
            'desc': desc,
            'details': details or {},
            'screen': self.current_screen_name,
            'vol': self.volume_level,
            'sig': self.signal_level,
            'bat': self.power_level
        }
        self.event_log.append(entry)
        if len(self.event_log) > 200:
            self.event_log.pop(0)

    # ========================================================================
    # Direct Firmware Functions (app_lcd.c)
    # ========================================================================

    def appLcdenable(self):
        """Replicates appLcdenable() in app_lcd.c:1260-1277"""
        self.Paytm_LCD_Set_PayTm_En(False)
        self.Paytm_LCD_Set_Power_Levl(4)
        self.Paytm_LCD_Set_Signal_Levl(4)
        self.Paytm_LCD_Set_Volume_Levl(self.volume_level)
        self.log_event("LCD_INIT", "appLcdenable()", "Initialized TM1726 registers; power=4, signal=4, vol=" + str(self.volume_level))
        self._notify()

    def appLcdenablecharger(self):
        """Replicates appLcdenablecharger() in app_lcd.c:1279-1290"""
        self.Paytm_LCD_Set_Power_Levl(0)
        self.log_event("CHARGER_PLUG", "appLcdenablecharger()", "VBUS detected; set power icon for charging")
        self._notify()

    def Paytm_LCD_Set_RS_En(self, en: bool):
        """Replicates Paytm_LCD_Set_RS_En() in app_lcd.c:773-784"""
        r, c = ROW(self.icon_rs), COM(self.icon_rs)
        if en:
            self.disp_buf[r] |= (1 << c)
        else:
            self.disp_buf[r] &= ~(1 << c) & 0xFF
        self.rs_en = en

    def Paytm_LCD_Set_Hook_En(self, en: bool):
        """Replicates Paytm_LCD_Set_Hook_En() in app_lcd.c:786-797"""
        r, c = ROW(self.icon_hook), COM(self.icon_hook)
        if en:
            self.disp_buf[r] |= (1 << c)
        else:
            self.disp_buf[r] &= ~(1 << c) & 0xFF
        self.hook_en = en

    def Paytm_LCD_Set_Count_En(self, en: bool):
        """Replicates Paytm_LCD_Set_Count_En() in app_lcd.c:799-814"""
        r, c = ROW(self.icon_count), COM(self.icon_count)
        if en:
            self.disp_buf[r] |= (1 << c)
        else:
            self.disp_buf[r] &= ~(1 << c) & 0xFF
        self.count_en = en

    def Paytm_LCD_Set_PayTm_En(self, en: bool):
        """Replicates Paytm_LCD_Set_PayTm_En() in app_lcd.c:816-831"""
        r, c = ROW(self.icon_paytm), COM(self.icon_paytm)
        if en:
            self.disp_buf[r] |= (1 << c)
        else:
            self.disp_buf[r] &= ~(1 << c) & 0xFF
        self.paytm_en = en

    def Paytm_LCD_Set_Volume_Levl(self, level: int):
        """Replicates Paytm_LCD_Set_Volume_Levl() in app_lcd.c:896-936"""
        if level > 4:
            level = 4
        # Clear volume flags
        for pt in self.volume_flag_map:
            r, c = ROW(pt), COM(pt)
            self.disp_buf[r] &= ~(1 << c) & 0xFF

        if level > 0:
            for i in range(min(level, len(self.volume_flag_map))):
                r, c = ROW(self.volume_flag_map[i]), COM(self.volume_flag_map[i])
                self.disp_buf[r] |= (1 << c)
        self.volume_bar_level = level

    def Paytm_LCD_Set_Mute(self, en: bool):
        """Replicates Paytm_LCD_Set_Mute() in app_lcd.c:938-953"""
        for pt in self.icon_mute:
            r, c = ROW(pt), COM(pt)
            if en:
                self.disp_buf[r] |= (1 << c)
            else:
                self.disp_buf[r] &= ~(1 << c) & 0xFF
        self.is_mute = en

    def Paytm_LCD_Set_Signal_Levl(self, level: int):
        """Replicates Paytm_LCD_Set_Signal_Levl() in app_lcd.c:955-1001"""
        if level > 4:
            level = 4
        for pt in self.signal_flag_map:
            r, c = ROW(pt), COM(pt)
            self.disp_buf[r] &= ~(1 << c) & 0xFF

        for i in range(len(self.signal_flag_map)):
            r, c = ROW(self.signal_flag_map[i]), COM(self.signal_flag_map[i])
            if i > level:
                break
            else:
                self.disp_buf[r] |= (1 << c)
        self.signal_level = level

    def Paytm_LCD_Set_Power_Levl(self, level: int):
        """Replicates Paytm_LCD_Set_Power_Levl() in app_lcd.c:1003-1044"""
        if level > 4:
            level = 4
        for pt in self.power_flag_map:
            r, c = ROW(pt), COM(pt)
            self.disp_buf[r] &= ~(1 << c) & 0xFF

        for i in range(len(self.power_flag_map)):
            r, c = ROW(self.power_flag_map[i]), COM(self.power_flag_map[i])
            if i > level:
                break
            else:
                self.disp_buf[r] |= (1 << c)
        self.power_level = level

    def Paytm_LCD_Set_NoSignal_En(self, en: bool):
        """Replicates Paytm_LCD_Set_NoSignal_En() in app_lcd.c:1046-1057"""
        r, c = ROW(self.icon_nosignal), COM(self.icon_nosignal)
        if en:
            self.disp_buf[r] |= (1 << c)
        else:
            self.disp_buf[r] &= ~(1 << c) & 0xFF
        self.no_signal = en

    def Paytm_LCD_Set_NoSim_En(self, en: bool):
        """Replicates Paytm_LCD_Set_NoSim_En() in app_lcd.c:1059-1070"""
        r, c = ROW(self.icon_nosim), COM(self.icon_nosim)
        if en:
            self.disp_buf[r] |= (1 << c)
        else:
            self.disp_buf[r] &= ~(1 << c) & 0xFF
        self.no_sim = en

    def Paytm_LCD_Set_NoConnect_En(self, en: bool):
        """Replicates Paytm_LCD_Set_NoConnect_En() in app_lcd.c:1072-1080"""
        r, c = ROW(self.icon_noconnect), COM(self.icon_noconnect)
        if en:
            self.disp_buf[r] |= (1 << c)
        else:
            self.disp_buf[r] &= ~(1 << c) & 0xFF
        self.no_connect = en

    def prvLcdPointSet(self, level: int):
        """Replicates prvLcdPointSet() in app_lcd.c:104-139"""
        for pt in self.point_flag_map:
            r, c = ROW(pt), COM(pt)
            self.disp_buf[r] &= ~(1 << c) & 0xFF

        if level == 0:
            return

        if level <= 8:
            pt = self.point_flag_map[level - 1]
            r, c = ROW(pt), COM(pt)
            self.disp_buf[r] |= (1 << c)
        else:
            for pt in self.point_flag_map:
                r, c = ROW(pt), COM(pt)
                self.disp_buf[r] |= (1 << c)

    def prvLcdBigNumSet(self, amount: int):
        """Replicates prvLcdBigNumSet() in app_lcd.c:153-257"""
        # Clear large digits
        for i in range(8):
            for j in range(7):
                r, c = ROW(self.large_digits_map[i][j]), COM(self.large_digits_map[i][j])
                self.disp_buf[r] &= ~(1 << c) & 0xFF

        if amount < 0 and self.decimal <= 0:
            return

        filllen = self.front_fill if self.front_fill >= 0 else 0
        if filllen > 0:
            s = f"{amount:0{filllen}d}"
        else:
            s = str(amount)

        memoy_len = min(len(s), 8)
        for i in range(memoy_len):
            char_val = ord(s[i]) - ord('0')
            if 0 <= char_val <= 9:
                dig = FIRMWARE_SEG_MAP_DIGITS[char_val]
            else:
                dig, _, _ = encode_7segment_char(s[i])
            for j in range(7):
                r, c = ROW(self.large_digits_map[i][j]), COM(self.large_digits_map[i][j])
                if dig & 0x01:
                    self.disp_buf[r] |= (1 << c)
                else:
                    self.disp_buf[r] &= ~(1 << c) & 0xFF
                dig >>= 1

        if self.decimal > 0:
            self.prvLcdPointSet(memoy_len)
            str_de = f"{self.decimal:02d}" if self.decimal < 10 else str(self.decimal)
            length = min(memoy_len + 2, 8)
            for i in range(memoy_len, length):
                char_val = ord(str_de[i - memoy_len]) - ord('0')
                if 0 <= char_val <= 9:
                    dig = FIRMWARE_SEG_MAP_DIGITS[char_val]
                else:
                    dig = 0
                for j in range(7):
                    r, c = ROW(self.large_digits_map[i][j]), COM(self.large_digits_map[i][j])
                    if dig & 0x01:
                        self.disp_buf[r] |= (1 << c)
                    else:
                        self.disp_buf[r] &= ~(1 << c) & 0xFF
                    dig >>= 1

    def prvLcdReceiveAmountSet(self, amount: int):
        """Replicates prvLcdReceiveAmountSet() in app_lcd.c:420-473"""
        for i in range(4):
            for j in range(7):
                r, c = ROW(self.small_digits_map[i][j]), COM(self.small_digits_map[i][j])
                self.disp_buf[r] &= ~(1 << c) & 0xFF

        if amount < 0:
            return

        data = amount
        for i in range(4):
            num = data % 10
            dig = FIRMWARE_SEG_MAP_DIGITS[num]
            for j in range(7):
                r, c = ROW(self.small_digits_map[i][j]), COM(self.small_digits_map[i][j])
                if dig & 0x01:
                    self.disp_buf[r] |= (1 << c)
                else:
                    self.disp_buf[r] &= ~(1 << c) & 0xFF
                dig >>= 1
            data //= 10
            if i < 3 and data == 0:
                break

    def prvLcdReceiveAmountClr(self):
        """Replicates prvLcdReceiveAmountClr() in app_lcd.c:475-502"""
        for i in range(4):
            for j in range(7):
                r, c = ROW(self.small_digits_map[i][j]), COM(self.small_digits_map[i][j])
                self.disp_buf[r] &= ~(1 << c) & 0xFF

    def prvLcdBigNumClr(self):
        """Replicates prvLcdBigNumClr() in app_lcd.c:380-418"""
        for i in range(8):
            for j in range(7):
                r, c = ROW(self.large_digits_map[i][j]), COM(self.large_digits_map[i][j])
                self.disp_buf[r] &= ~(1 << c) & 0xFF

    def appLcdShowTextHitachi(self, remove_extra_i: bool = True):
        """Replicates appLcdShowTextHitachi() in app_lcd.c:1359-1430"""
        self.current_screen_name = "HITACHI_IDLE"

        # Show total count if transactions exist
        if self.total_txn_count > 0:
            self.Paytm_LCD_Set_Count_En(True)
            self.prvLcdReceiveAmountSet(self.total_txn_count)
        else:
            self.Paytm_LCD_Set_Count_En(False)
            self.prvLcdReceiveAmountClr()

        # Clear Hook (row 13 bit 2) and Paytm (row 13 bit 3)
        self.disp_buf[13] &= ~((1 << 2) | (1 << 3)) & 0xFF
        # Clear RS (row 10 bit 0)
        self.disp_buf[10] &= ~(1 << 0) & 0xFF
        self.hook_en = False
        self.paytm_en = False
        self.rs_en = False

        # Clear all decimal points (COM 3 on rows 0-7)
        for r in range(8):
            self.disp_buf[r] &= ~(1 << 3) & 0xFF

        patterns = HITACHI_PATTERNS_CLEAN_T if remove_extra_i else FIRMWARE_HITACHI_PATTERNS
        # Draw "HITACHI" on all 8 digits (if remove_extra_i: Digit 3 is 0x01 instead of 0x31)
        for i in range(8):
            dig = patterns[i]
            for j in range(7):
                r, c = ROW(self.large_digits_map[i][j]), COM(self.large_digits_map[i][j])
                if dig & 0x01:
                    self.disp_buf[r] |= (1 << c)
                else:
                    self.disp_buf[r] &= ~(1 << c) & 0xFF
                dig >>= 1

        desc = "Idle screen restored; showing 'HITACHI' (Clean 'T', extra 'I' removed)" if remove_extra_i else "Idle screen restored; showing 'HITACHI'"
        self.is_payment_active = False
        self.log_event("IDLE_TIMEOUT", "appLcdShowTextHitachi()", desc)
        self._notify()

    def appLcdShowTime(self, hour: Optional[int] = None, min: Optional[int] = None, sec: Optional[int] = None):
        """
        Replicates appLcdShowTime() in app_lcd.c
        Formats: [blank] [H1] [H0.] [M1] [M0.] [S1] [S0] [blank]
        """
        if hour is None or min is None or sec is None:
            now = datetime.datetime.now()
            hour = hour if hour is not None else now.hour
            min = min if min is not None else now.minute
            sec = sec if sec is not None else now.second

        self.current_screen_name = "TIME_DISPLAY"

        # Keep Day count badge and count digits visible if count > 0
        if self.total_txn_count > 0:
            self.Paytm_LCD_Set_Count_En(True)
            self.prvLcdReceiveAmountSet(self.total_txn_count)
        else:
            self.Paytm_LCD_Set_Count_En(False)
            self.prvLcdReceiveAmountClr()

        # Clear Hook (row 13 bit 2) and Paytm (row 13 bit 3)
        self.disp_buf[13] &= ~((1 << 2) | (1 << 3)) & 0xFF
        # Clear RS (row 10 bit 0)
        self.disp_buf[10] &= ~(1 << 0) & 0xFF
        self.hook_en = False
        self.paytm_en = False
        self.rs_en = False

        # Clear all decimal points (COM 3 on rows 0-7)
        for r in range(8):
            self.disp_buf[r] &= ~(1 << 3) & 0xFF

        # Set decimal points after Digit 2 and Digit 4 (Row 5 and Row 3)
        self.disp_buf[5] |= (1 << 3)
        self.disp_buf[3] |= (1 << 3)

        # 6 digits centered symmetrically across 8 positions (indices 1..6)
        h1 = (hour // 10) % 10
        h0 = hour % 10
        m1 = (min // 10) % 10
        m0 = min % 10
        s1 = (sec // 10) % 10
        s0 = sec % 10

        patterns = [
            0x00, # Digit 0: blank
            FIRMWARE_SEG_MAP_DIGITS[h1],
            FIRMWARE_SEG_MAP_DIGITS[h0],
            FIRMWARE_SEG_MAP_DIGITS[m1],
            FIRMWARE_SEG_MAP_DIGITS[m0],
            FIRMWARE_SEG_MAP_DIGITS[s1],
            FIRMWARE_SEG_MAP_DIGITS[s0],
            0x00  # Digit 7: blank
        ]

        for i in range(8):
            dig = patterns[i]
            for j in range(7):
                r, c = ROW(self.large_digits_map[i][j]), COM(self.large_digits_map[i][j])
                if dig & 0x01:
                    self.disp_buf[r] |= (1 << c)
                else:
                    self.disp_buf[r] &= ~(1 << c) & 0xFF
                dig >>= 1

        self.log_event("TIME_SHOW", "appLcdShowTime()", f"Showing Time: {hour:02d}.{min:02d}.{sec:02d}")
        self._notify()

    def appLcdShowDate(self, day: Optional[int] = None, mon: Optional[int] = None, year: Optional[int] = None):
        """
        Replicates appLcdShowDate() in app_lcd.c
        Formats: [blank] [D1] [D0.] [M1] [M0.] [Y1] [Y0] [blank]
        """
        if day is None or mon is None or year is None:
            now = datetime.datetime.now()
            day = day if day is not None else now.day
            mon = mon if mon is not None else now.month
            year = year if year is not None else now.year

        self.current_screen_name = "DATE_DISPLAY"

        if self.total_txn_count > 0:
            self.Paytm_LCD_Set_Count_En(True)
            self.prvLcdReceiveAmountSet(self.total_txn_count)
        else:
            self.Paytm_LCD_Set_Count_En(False)
            self.prvLcdReceiveAmountClr()

        self.disp_buf[13] &= ~((1 << 2) | (1 << 3)) & 0xFF
        self.disp_buf[10] &= ~(1 << 0) & 0xFF
        self.hook_en = False
        self.paytm_en = False
        self.rs_en = False

        for r in range(8):
            self.disp_buf[r] &= ~(1 << 3) & 0xFF

        self.disp_buf[5] |= (1 << 3)
        self.disp_buf[3] |= (1 << 3)

        d1 = (day // 10) % 10
        d0 = day % 10
        m1 = (mon // 10) % 10
        m0 = min if False else mon % 10
        yr_2digit = year % 100
        y1 = (yr_2digit // 10) % 10
        y0 = yr_2digit % 10

        patterns = [
            0x00,
            FIRMWARE_SEG_MAP_DIGITS[d1],
            FIRMWARE_SEG_MAP_DIGITS[d0],
            FIRMWARE_SEG_MAP_DIGITS[m1],
            FIRMWARE_SEG_MAP_DIGITS[m0],
            FIRMWARE_SEG_MAP_DIGITS[y1],
            FIRMWARE_SEG_MAP_DIGITS[y0],
            0x00
        ]

        for i in range(8):
            dig = patterns[i]
            for j in range(7):
                r, c = ROW(self.large_digits_map[i][j]), COM(self.large_digits_map[i][j])
                if dig & 0x01:
                    self.disp_buf[r] |= (1 << c)
                else:
                    self.disp_buf[r] &= ~(1 << c) & 0xFF
                dig >>= 1

        self.log_event("DATE_SHOW", "appLcdShowDate()", f"Showing Date: {day:02d}.{mon:02d}.{yr_2digit:02d}")
        self._notify()

    def step_rotating_idle(self) -> str:
        """
        Advances the rotating idle state machine:
        Stage 0: HITACHI
        Stage 1: TIME (HH.MM.SS)
        Stage 2: DATE (DD.MM.YY)
        """
        if self.is_payment_active:
            return "PAYMENT_ACTIVE"

        if self.rotating_idle_stage == 0:
            self.appLcdShowTextHitachi(remove_extra_i=True)
            self.rotating_idle_stage = 1
            return "HITACHI"
        elif self.rotating_idle_stage == 1:
            self.appLcdShowTime()
            self.rotating_idle_stage = 2
            return "TIME"
        else:
            self.appLcdShowDate()
            self.rotating_idle_stage = 0
            return "DATE"

    def appLcdShowTextLIT(self):
        """Replicates appLcdShowTextLIT() in app_lcd.c:341-379"""
        self.current_screen_name = "LIT_BRAND"
        self.prvLcdBigNumClr()
        self.disp_buf[13] &= ~(1 << 3) & 0xFF  # Clear Paytm

        for i in range(3):
            dig = FIRMWARE_LIT_PATTERNS[i]
            for j in range(7):
                r, c = ROW(self.large_digits_map[i][j]), COM(self.large_digits_map[i][j])
                if dig & 0x01:
                    self.disp_buf[r] |= (1 << c)
                else:
                    self.disp_buf[r] &= ~(1 << c) & 0xFF
                dig >>= 1

        self.log_event("BRAND_SHOW", "appLcdShowTextLIT()", "Brand screen active; showing 'LIT'")
        self._notify()

    def appLcdset(self, lcd_buf: Dict):
        """Replicates appLcdset() in app_lcd.c:1193-1230"""
        self.decimal = lcd_buf.get('lcd_decimalnum', 0)
        self.front_fill = lcd_buf.get('lcd_fill', 0)

        self.Paytm_LCD_Set_Hook_En(lcd_buf.get('lcd_hook', False))
        self.Paytm_LCD_Set_RS_En(lcd_buf.get('lcd_rs', False))
        self.Paytm_LCD_Set_Count_En(lcd_buf.get('lcd_count', False))

        if lcd_buf.get('lcd_count', False):
            self.prvLcdReceiveAmountSet(lcd_buf.get('lcd_amount', 0))
        else:
            self.prvLcdReceiveAmountClr()

        self.prvLcdBigNumSet(lcd_buf.get('lcd_num', 0))
        self._notify()

    def appLcdDisplayEnableALL(self, enable: bool):
        """Replicates appLcdDisplayEnableALL() in app_lcd.c:1232-1247 (LCD Test mode)"""
        fill = 0xFF if enable else 0x00
        self.disp_buf = [fill] * 18
        self.current_screen_name = "ALL_SEGMENTS_TEST" if enable else "BLANK_SCREEN"
        self.log_event("LCD_TEST", "appLcdDisplayEnableALL()", f"All LCD segments {'LIT' if enable else 'OFF'}")
        self._notify()

    # ========================================================================
    # Interactive Event Handlers (Simulating Firmware Callbacks)
    # ========================================================================

    def simulate_key_vol_up(self):
        """Simulates MSG_KEY_UP in customer_handlers.c:895-908"""
        old_vol = self.volume_level
        # Note: bug in customer_handlers.c line 895 checked vol < 7 instead of VOL_MAX (8)
        new_vol = min(self.volume_level + 1, 8)
        self.volume_level = new_vol
        # Map 1-8 volume scale to 1-4 LCD level indicator
        lcd_level = max(1, (new_vol + 1) // 2)
        self.Paytm_LCD_Set_Volume_Levl(lcd_level)
        self.log_event(
            "MSG_KEY_UP",
            "customer_msg_handler() -> MSG_KEY_UP",
            f"Volume Up pressed: {old_vol} -> {new_vol} (LCD bars: {lcd_level})",
            {'pin': 33, 'prev_vol': old_vol, 'new_vol': new_vol}
        )
        self._notify()

    def simulate_key_vol_down(self):
        """Simulates MSG_KEY_DOWN in customer_handlers.c:916-929"""
        old_vol = self.volume_level
        new_vol = max(self.volume_level - 1, 1)
        self.volume_level = new_vol
        lcd_level = max(1, (new_vol + 1) // 2)
        self.Paytm_LCD_Set_Volume_Levl(lcd_level)
        self.log_event(
            "MSG_KEY_DOWN",
            "customer_msg_handler() -> MSG_KEY_DOWN",
            f"Volume Down pressed: {old_vol} -> {new_vol} (LCD bars: {lcd_level})",
            {'pin': 34, 'prev_vol': old_vol, 'new_vol': new_vol}
        )
        self._notify()

    def simulate_payment_received(self, rupees: int, paise: int = 0, txn_id: str = "415857764147", is_replay: bool = False):
        """
        Simulates payment broadcast in customer_payment_handler.c:655-680
        """
        if not is_replay:
            self.total_txn_count += 1
            self.last_payment_amount = rupees + (paise / 100.0)

        lcd_param = {
            'lcd_hook': True,           # Checkmark
            'lcd_rs': True,             # ₹/Rs. icon
            'lcd_num': rupees,          # Main amount
            'lcd_decimalnum': paise,    # Decimal cents
            'lcd_count': True,          # COUNT badge
            'lcd_amount': self.total_txn_count, # Txn number
            'lcd_fill': 0
        }
        self.is_payment_active = True
        self.current_screen_name = "PAYMENT_ACTIVE"
        self.appLcdset(lcd_param)
        self.log_event(
            "PAYMENT_BROADCAST",
            "customer_payment_broadcast()",
            f"Payment of Rs.{rupees}.{paise:02d} received (Txn #{self.total_txn_count}, ID: {txn_id}, Replay: {is_replay})",
            {'rupees': rupees, 'paise': paise, 'txn_id': txn_id, 'is_replay': is_replay}
        )

    def simulate_idle_timeout(self):
        """Simulates 10s idle timeout callback (prv_lcd_idle_work_callback) resuming rotating idle screen."""
        self.is_payment_active = False
        self.appLcdShowTextHitachi(remove_extra_i=True)
        self.rotating_idle_stage = 1  # Next rotation after 5s is TIME

    def simulate_network_status(self, status: str):
        """
        Simulates network transitions: 'CONNECTED', 'DISCONNECTED', 'NO_SIM', 'NO_SIGNAL'
        """
        if status == 'CONNECTED':
            self.Paytm_LCD_Set_NoSim_En(False)
            self.Paytm_LCD_Set_NoSignal_En(False)
            self.Paytm_LCD_Set_NoConnect_En(False)
            self.Paytm_LCD_Set_Signal_Levl(4)
            self.log_event("MSG_NET_CONNECTED", "customer_msg_handler()", "Network registered & connected; 4 signal bars")
        elif status == 'DISCONNECTED':
            self.Paytm_LCD_Set_Signal_Levl(0)
            self.Paytm_LCD_Set_NoConnect_En(True)
            self.log_event("MSG_NET_DISCONNECTED", "customer_msg_handler()", "Network lost; NO CONNECT icon lit")
        elif status == 'NO_SIM':
            self.Paytm_LCD_Set_Signal_Levl(0)
            self.Paytm_LCD_Set_NoSim_En(True)
            self.log_event("MSG_NET_SIM_NOT_READY", "customer_msg_handler()", "SIM card missing/unreadable; NO SIM icon lit")
        elif status == 'NO_SIGNAL':
            self.Paytm_LCD_Set_Signal_Levl(0)
            self.Paytm_LCD_Set_NoSignal_En(True)
            self.log_event("MSG_NET_NO_SIGNAL", "customer_msg_handler()", "No cellular coverage; NO SIGNAL icon lit")
        self._notify()

    def simulate_battery_change(self, level: int):
        """Simulates MSG_BATTERY_STATUS in customer_handlers.c:985-995"""
        self.Paytm_LCD_Set_Power_Levl(level)
        self.log_event("MSG_BATTERY_STATUS", "customer_msg_handler()", f"Battery level update: {level}/4 bars")
        self._notify()

    def get_disp_buf(self) -> List[int]:
        """Returns the raw 18-byte TM1726 display register buffer."""
        return list(self.disp_buf)
