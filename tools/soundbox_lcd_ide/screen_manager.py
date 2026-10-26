"""
Screen Manager for SoundBox LCD Preview IDE
Contains pre-configured screen templates directly derived from firmware source code:
- customers/template/src/app_lcd.c
- customers/template/src/customer_payment_handler.c
- customers/template/src/customer_handlers.c
"""

from typing import Dict, List, Any, Tuple
from firmware_adapter import FirmwareLcdAdapter

class ScreenManager:
    """Manages pre-defined and custom screens for preview and navigation."""

    def __init__(self, firmware: FirmwareLcdAdapter):
        self.firmware = firmware
        self.current_screen_id = "idle_hitachi"

        # Pre-configured screen definitions backed by actual firmware code
        self.templates: Dict[str, Dict[str, Any]] = {
            "idle_hitachi": {
                "name": "1. Idle Screen ('HITACHI' - Clean Single Stem)",
                "mode": "SEGMENT",
                "source": "app_lcd.c:1359 (Clean 'T', extra 'I' removed)",
                "desc": "Single-stem 'T' with the extra 'I' removed (Digit 2=0x07, Digit 3=0x01).",
                "apply": lambda: self._apply_idle_hitachi(clean_t=True)
            },
            "idle_hitachi_double": {
                "name": "1b. Idle Screen ('HITACHI' - Original Double Stem)",
                "mode": "SEGMENT",
                "source": "app_lcd.c:1359 (Original Firmware)",
                "desc": "Original double-stem 'T' with the extra 'I' on Digit 3 (Digit 2=0x07, Digit 3=0x31).",
                "apply": lambda: self._apply_idle_hitachi(clean_t=False)
            },
            "idle_time": {
                "name": "1c. Idle Screen (Local Time: '12.45.30')",
                "mode": "SEGMENT",
                "source": "app_lcd.c (appLcdShowTime)",
                "desc": "Current local time in HH.MM.SS format with colon dot simulation on middle 8 digits.",
                "apply": lambda: self._apply_idle_time()
            },
            "idle_date": {
                "name": "1d. Idle Screen (Current Date: '24.10.26')",
                "mode": "SEGMENT",
                "source": "app_lcd.c (appLcdShowDate)",
                "desc": "Current date in DD.MM.YY format with separators on middle 8 digits.",
                "apply": lambda: self._apply_idle_date()
            },
            "payment_1590": {
                "name": "2. Payment Active (₹1,590.00)",
                "mode": "SEGMENT",
                "source": "customer_payment_handler.c:655 (customer_payment_broadcast)",
                "desc": "Real test transaction: Hook ✓, Rs. icon, 1590.00, count badge, signal, battery.",
                "apply": lambda: self._apply_payment(1590, 0, "415857764147")
            },
            "payment_250_50": {
                "name": "3. Payment Active (₹250.50)",
                "mode": "SEGMENT",
                "source": "customer_payment_handler.c:655 (customer_payment_broadcast)",
                "desc": "Transaction with paise decimal points: Hook ✓, Rs., 250.50.",
                "apply": lambda: self._apply_payment(250, 50, "415857764148")
            },
            "payment_large": {
                "name": "4. Large Payment (₹99,999.00)",
                "mode": "SEGMENT",
                "source": "customer_payment_handler.c:655 (Boundary Test)",
                "desc": "Tests multi-digit amount rendering approaching 8-digit capacity.",
                "apply": lambda: self._apply_payment(99999, 0, "415857764149")
            },
            "volume_level_6": {
                "name": "5. Volume Setting (Level 6)",
                "mode": "SEGMENT",
                "source": "customer_handlers.c:895 (MSG_KEY_UP)",
                "desc": "Live volume adjustment feedback with 3/4 volume bars.",
                "apply": lambda: self._apply_volume(6)
            },
            "brand_lit": {
                "name": "6. Brand Screen ('LIT')",
                "mode": "SEGMENT",
                "source": "app_lcd.c:341 (appLcdShowTextLIT)",
                "desc": "Manufacturer brand display: 'LIT' across digits 0-2.",
                "apply": lambda: self.firmware.appLcdShowTextLIT()
            },
            "no_sim_error": {
                "name": "7. Network Error (NO SIM)",
                "mode": "SEGMENT",
                "source": "customer_handlers.c:1059 (MSG_NET_SIM_NOT_READY)",
                "desc": "SIM missing or unreadable. NO SIM badge lit, signal 0 bars.",
                "apply": lambda: self._apply_no_sim()
            },
            "no_signal_error": {
                "name": "8. Network Error (NO SIGNAL)",
                "mode": "SEGMENT",
                "source": "customer_handlers.c:1046 (MSG_NET_NO_SIGNAL)",
                "desc": "Cellular base station unreachable. NO SIGNAL badge lit.",
                "apply": lambda: self._apply_no_signal()
            },
            "no_connect_error": {
                "name": "9. Server Error (NO CONNECT)",
                "mode": "SEGMENT",
                "source": "customer_handlers.c:1072 (MSG_NET_CONNECT_FAIL)",
                "desc": "TCP/MQTT broker connection failed. NO CONNECT badge lit.",
                "apply": lambda: self._apply_no_connect()
            },
            "charger_plugged": {
                "name": "10. Charger Plugged (VBUS)",
                "mode": "SEGMENT",
                "source": "app_lcd.c:1279 (appLcdenablecharger)",
                "desc": "Power key VBUS detection event; sets battery animation.",
                "apply": lambda: self.firmware.appLcdenablecharger()
            },
            "lcd_all_test": {
                "name": "11. Hardware Test (All Segments)",
                "mode": "SEGMENT",
                "source": "app_lcd.c:1232 (appLcdDisplayEnableALL)",
                "desc": "Diagnostic mode: all 18 bytes of TM1726 filled with 0xFF.",
                "apply": lambda: self.firmware.appLcdDisplayEnableALL(True)
            },
            "graphic_tft_payment": {
                "name": "12. Graphic TFT: Dynamic QR Payment",
                "mode": "GRAPHIC",
                "source": "framework_lcd9853.c (240x240 Color TFT)",
                "desc": "Dynamic UPI QR code screen with amount and countdown timer.",
                "elements": [
                    {"type": "text", "text": "Hitachi SoundBox", "x": 120, "y": 14, "font_size": 12, "color": "#FF9900", "anchor": "center"},
                    {"type": "qr_placeholder", "x": 60, "y": 32, "size": 120},
                    {"type": "text", "text": "Scan to Pay", "x": 120, "y": 160, "font_size": 10, "color": "#AAAAAA", "anchor": "center"},
                    {"type": "text", "text": "₹ 1,590.00", "x": 120, "y": 182, "font_size": 16, "color": "#20EE44", "anchor": "center"},
                    {"type": "text", "text": "Txn: 415857764147", "x": 120, "y": 210, "font_size": 9, "color": "#888888", "anchor": "center"}
                ]
            },
            "graphic_tft_idle": {
                "name": "13. Graphic TFT: System Idle Screen",
                "mode": "GRAPHIC",
                "source": "framework_lcd9853.c (240x240 Color TFT)",
                "desc": "Graphic idle screen with digital clock, battery, and network status.",
                "elements": [
                    {"type": "text", "text": "HITACHI", "x": 120, "y": 25, "font_size": 18, "color": "#FFFFFF", "anchor": "center"},
                    {"type": "text", "text": "16:20", "x": 120, "y": 80, "font_size": 36, "color": "#00D5FF", "anchor": "center"},
                    {"type": "text", "text": "Saturday, Oct 24", "x": 120, "y": 125, "font_size": 11, "color": "#AAAAAA", "anchor": "center"},
                    {"type": "rect", "x": 30, "y": 155, "width": 180, "height": 45, "fill": "#111111", "color": "#333333"},
                    {"type": "text", "text": "4G VoLTE: Connected", "x": 40, "y": 165, "font_size": 9, "color": "#20EE44", "anchor": "nw"},
                    {"type": "text", "text": "Battery: 85% (Charging)", "x": 40, "y": 182, "font_size": 9, "color": "#FFAA00", "anchor": "nw"},
                    {"type": "text", "text": "Ready for Transactions", "x": 120, "y": 220, "font_size": 10, "color": "#666666", "anchor": "center"}
                ]
            }
        }

    def get_screen_list(self) -> List[Tuple[str, str, str]]:
        """Returns list of (screen_id, name, mode) for UI screen selector."""
        return [(sid, data["name"], data["mode"]) for sid, data in self.templates.items()]

    def select_screen(self, screen_id: str, canvas=None) -> Dict:
        """Applies the selected screen to the firmware emulator and canvas."""
        if screen_id not in self.templates:
            return {}

        self.current_screen_id = screen_id
        tpl = self.templates[screen_id]
        mode = tpl.get("mode", "SEGMENT")

        if canvas:
            canvas.set_mode(mode)

        if mode == "SEGMENT":
            if "apply" in tpl and callable(tpl["apply"]):
                tpl["apply"]()
        elif mode == "GRAPHIC" and canvas:
            canvas.graphic_elements = list(tpl.get("elements", []))
            canvas.render()

        return tpl

    # ------------------------------------------------------------------------
    # Private Helper Setups
    # ------------------------------------------------------------------------

    def _apply_idle_hitachi(self, clean_t: bool = True):
        self.firmware.appLcdenable()
        self.firmware.total_txn_count = 5  # Matches real device photo: Day Count 5
        self.firmware.appLcdShowTextHitachi(remove_extra_i=clean_t)

    def _apply_idle_time(self):
        self.firmware.appLcdenable()
        self.firmware.total_txn_count = 5
        self.firmware.appLcdShowTime(12, 45, 30)

    def _apply_idle_date(self):
        self.firmware.appLcdenable()
        self.firmware.total_txn_count = 5
        self.firmware.appLcdShowDate(24, 10, 26)

    def _apply_payment(self, rupees: int, paise: int, txn_id: str):
        self.firmware.simulate_payment_received(rupees, paise, txn_id=txn_id)

    def _apply_volume(self, vol: int):
        self.firmware.volume_level = vol
        self.firmware.Paytm_LCD_Set_Volume_Levl((vol + 1) // 2)

    def _apply_no_sim(self):
        self.firmware.simulate_network_status('NO_SIM')

    def _apply_no_signal(self):
        self.firmware.simulate_network_status('NO_SIGNAL')

    def _apply_no_connect(self):
        self.firmware.simulate_network_status('DISCONNECTED')
