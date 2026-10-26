"""
Automated Unit Tests for SoundBox LCD Preview IDE
Verifies:
1. Exact 7-segment encoding and special multi-digit patterns (Hitachi, LIT).
2. TM1726 disp_buf[18] bitwise register operations.
3. Numeric amount rendering and decimal point positioning.
4. Graphic font metrics, text bounding boxes, line wrapping, and clipping.
5. Simulated hardware callbacks and state transitions.
6. Project JSON serialization and deserialization.
7. Pure-Python raw BMP and PNG generation.
"""

import os
import sys
import unittest
import json

# Add parent directory to module path
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
sys.path.insert(0, parent_dir)

from font_manager import (
    FIRMWARE_SEG_MAP_DIGITS,
    FIRMWARE_HITACHI_PATTERNS,
    FIRMWARE_LIT_PATTERNS,
    encode_7segment_char,
    encode_7segment_string,
    GraphicFontMetrics
)
from firmware_adapter import FirmwareLcdAdapter, COM, ROW, COM_ROW
from screen_manager import ScreenManager
from project_manager import ProjectManager
from stress_tester import StressTesterPanel, DEFAULT_TEST_CASES


class TestFirmware7SegmentFonts(unittest.TestCase):
    """Verifies that all 7-segment bit patterns match firmware code exactly."""

    def test_digit_0_to_9_patterns(self):
        # 0: A+B+C+D+E+F = 0x3F
        self.assertEqual(FIRMWARE_SEG_MAP_DIGITS[0], 0x3F)
        # 1: B+C = 0x06
        self.assertEqual(FIRMWARE_SEG_MAP_DIGITS[1], 0x06)
        # 8: All segments = 0x7F
        self.assertEqual(FIRMWARE_SEG_MAP_DIGITS[8], 0x7F)
        # Verify 10 entries in table
        self.assertEqual(len(FIRMWARE_SEG_MAP_DIGITS), 10)

    def test_hitachi_patterns(self):
        # Must match app_lcd.c:1394-1403 exactly
        self.assertEqual(len(FIRMWARE_HITACHI_PATTERNS), 8)
        self.assertEqual(FIRMWARE_HITACHI_PATTERNS[0], 0x76) # 'H'
        self.assertEqual(FIRMWARE_HITACHI_PATTERNS[1], 0x06) # 'I'
        self.assertEqual(FIRMWARE_HITACHI_PATTERNS[2], 0x07) # 'T' left half
        self.assertEqual(FIRMWARE_HITACHI_PATTERNS[3], 0x31) # 'T' right half
        self.assertEqual(FIRMWARE_HITACHI_PATTERNS[4], 0x77) # 'A'
        self.assertEqual(FIRMWARE_HITACHI_PATTERNS[5], 0x39) # 'C'
        self.assertEqual(FIRMWARE_HITACHI_PATTERNS[6], 0x76) # 'H'
        self.assertEqual(FIRMWARE_HITACHI_PATTERNS[7], 0x06) # 'I'

    def test_lit_patterns(self):
        # Must match app_lcd.c:362
        self.assertEqual(FIRMWARE_LIT_PATTERNS[0], 0x38) # 'L'
        self.assertEqual(FIRMWARE_LIT_PATTERNS[1], 0x06) # 'I'
        self.assertEqual(FIRMWARE_LIT_PATTERNS[2], 0x78) # 't'

    def test_hitachi_string_encoding(self):
        encoded = encode_7segment_string("HITACHI", max_digits=8)
        self.assertEqual(len(encoded), 8)
        for i in range(8):
            self.assertEqual(encoded[i]['pattern'], FIRMWARE_HITACHI_PATTERNS[i])
            self.assertTrue(encoded[i]['supported'])

    def test_lit_string_encoding(self):
        encoded = encode_7segment_string("LIT", max_digits=8)
        self.assertEqual(len(encoded), 3)
        for i in range(3):
            self.assertEqual(encoded[i]['pattern'], FIRMWARE_LIT_PATTERNS[i])

    def test_unsupported_characters(self):
        # 'W' and 'M' cannot be formed on 7-segment
        pat, supp, approx = encode_7segment_char('W')
        self.assertFalse(supp)
        pat, supp, approx = encode_7segment_char('M')
        self.assertFalse(supp)
        # 'K' is unsupported
        pat, supp, approx = encode_7segment_char('K')
        self.assertFalse(supp)


class TestFirmwareAdapter(unittest.TestCase):
    """Verifies register-level TM1726 memory behavior and event simulation."""

    def setUp(self):
        self.fw = FirmwareLcdAdapter()

    def test_com_row_macros(self):
        val = COM_ROW(4, 7)
        self.assertEqual(COM(val), 4)
        self.assertEqual(ROW(val), 7)

    def test_disp_buf_initial_size(self):
        buf = self.fw.get_disp_buf()
        self.assertEqual(len(buf), 18)

    def test_hitachi_idle_state(self):
        self.fw.appLcdShowTextHitachi()
        self.assertEqual(self.fw.current_screen_name, "HITACHI_IDLE")
        self.assertFalse(self.fw.hook_en)
        self.assertFalse(self.fw.rs_en)

    def test_payment_broadcast(self):
        self.fw.total_txn_count = 0
        self.fw.simulate_payment_received(1590, 0, txn_id="TEST_001")
        self.assertEqual(self.fw.current_screen_name, "PAYMENT_ACTIVE")
        self.assertTrue(self.fw.hook_en)
        self.assertTrue(self.fw.rs_en)
        self.assertTrue(self.fw.count_en)
        self.assertEqual(self.fw.total_txn_count, 1)

    def test_volume_controls(self):
        self.fw.volume_level = 1
        self.fw.simulate_key_vol_up()
        self.assertEqual(self.fw.volume_level, 2)
        self.fw.simulate_key_vol_down()
        self.assertEqual(self.fw.volume_level, 1)
        # Test boundary minimum (cannot go below 1)
        self.fw.simulate_key_vol_down()
        self.assertEqual(self.fw.volume_level, 1)

    def test_network_status_transitions(self):
        self.fw.simulate_network_status('DISCONNECTED')
        self.assertTrue(self.fw.no_connect)
        self.assertEqual(self.fw.signal_level, 0)

        self.fw.simulate_network_status('CONNECTED')
        self.assertFalse(self.fw.no_connect)
        self.assertEqual(self.fw.signal_level, 4)

    def test_all_display_enable_test(self):
        self.fw.appLcdDisplayEnableALL(True)
        buf = self.fw.get_disp_buf()
        for b in buf:
            self.assertEqual(b, 0xFF)

        self.fw.appLcdDisplayEnableALL(False)
        buf = self.fw.get_disp_buf()
        for b in buf:
            self.assertEqual(b, 0x00)


class TestGraphicFontMetrics(unittest.TestCase):
    """Verifies graphic font calculations, bounding boxes, and line wraps."""

    def test_ascii_5x7_metrics(self):
        w, h = GraphicFontMetrics.measure_text("HITACHI", "ASCII_5x7")
        # 7 chars * 5px = 35px
        self.assertEqual(w, 35)
        self.assertEqual(h, 7)

    def test_ascii_8x16_metrics(self):
        w, h = GraphicFontMetrics.measure_text("12345", "ASCII_8x16")
        # 5 chars * 8px = 40px
        self.assertEqual(w, 40)
        self.assertEqual(h, 16)

    def test_multiline_measurement(self):
        text = "Hello\nWorld"
        w, h = GraphicFontMetrics.measure_text(text, "ASCII_8x16")
        self.assertEqual(w, 40)  # max line is 5 chars * 8 = 40
        # 2 lines * 16px + 3px spacing = 35px
        self.assertEqual(h, 35)

    def test_line_wrapping(self):
        text = "The quick brown fox jumps over the lazy dog"
        # 8px per char -> 80px max width allows ~10 chars per line
        lines = GraphicFontMetrics.wrap_text(text, max_width_px=80, font_name="ASCII_8x16")
        self.assertTrue(len(lines) > 1)
        for line in lines:
            w, _ = GraphicFontMetrics.measure_text(line, "ASCII_8x16")
            # Word wrapping should respect max width where possible
            self.assertTrue(w <= 80 or len(line.split()) == 1)


class TestScreenManager(unittest.TestCase):
    """Verifies screen templates and navigation."""

    def setUp(self):
        self.fw = FirmwareLcdAdapter()
        self.sm = ScreenManager(self.fw)

    def test_templates_exist(self):
        screens = self.sm.get_screen_list()
        self.assertTrue(len(screens) >= 10)
        screen_ids = [s[0] for s in screens]
        self.assertIn("idle_hitachi", screen_ids)
        self.assertIn("payment_1590", screen_ids)
        self.assertIn("brand_lit", screen_ids)
        self.assertIn("no_sim_error", screen_ids)

    def test_select_screen(self):
        tpl = self.sm.select_screen("payment_1590")
        self.assertIsNotNone(tpl)
        self.assertEqual(self.fw.current_screen_name, "PAYMENT_ACTIVE")


class TestProjectManager(unittest.TestCase):
    """Verifies project persistence and raw image export without external dependencies."""

    def test_save_and_load_project(self):
        test_file = os.path.join(current_dir, "test_proj.json")
        sample_data = {
            "version": "1.0",
            "mode": "SEGMENT",
            "palette": "Amber",
            "disp_buf": [1, 2, 3] * 6,
            "volume": 4
        }
        try:
            saved = ProjectManager.save_project(test_file, sample_data)
            self.assertTrue(saved)
            loaded = ProjectManager.load_project(test_file)
            self.assertIsNotNone(loaded)
            self.assertEqual(loaded["version"], "1.0")
            self.assertEqual(loaded["volume"], 4)
            self.assertEqual(loaded["disp_buf"], sample_data["disp_buf"])
        finally:
            if os.path.exists(test_file):
                os.remove(test_file)

    def test_pure_python_bmp_export(self):
        test_bmp = os.path.join(current_dir, "test_out.bmp")
        w, h = 10, 10
        rgb = b'\xFF\x99\x00' * (w * h)
        try:
            ok = ProjectManager.export_raw_bmp(w, h, rgb, test_bmp)
            self.assertTrue(ok)
            self.assertTrue(os.path.exists(test_bmp))
            # Verify BMP header magic 'BM'
            with open(test_bmp, 'rb') as f:
                sig = f.read(2)
                self.assertEqual(sig, b'BM')
        finally:
            if os.path.exists(test_bmp):
                os.remove(test_bmp)

    def test_pure_python_png_export(self):
        test_png = os.path.join(current_dir, "test_out.png")
        w, h = 10, 10
        rgb = b'\x00\xFF\x66' * (w * h)
        try:
            ok = ProjectManager.export_raw_png(w, h, rgb, test_png)
            self.assertTrue(ok)
            self.assertTrue(os.path.exists(test_png))
            # Verify PNG signature
            with open(test_png, 'rb') as f:
                sig = f.read(8)
                self.assertEqual(sig, b'\x89PNG\r\n\x1a\n')
        finally:
            if os.path.exists(test_png):
                os.remove(test_png)

    def test_c_header_stub_generation(self):
        disp_buf = [0x76, 0x06, 0x07, 0x31] + [0] * 14
        code = ProjectManager.export_c_header_stub("HITACHI", disp_buf)
        self.assertIn("s_custom_screen_hitachi", code)
        self.assertIn("0x76", code)
        self.assertIn("0x07", code)
        self.assertIn("dispDma(0xC0, disp_buf, 15);", code)

    def test_show_time_display(self):
        fw = FirmwareLcdAdapter()
        fw.appLcdShowTime(12, 45, 30)
        self.assertEqual(fw.current_screen_name, "TIME_DISPLAY")
        # Check decimal point colons on row 5 and row 3
        self.assertTrue(bool(fw.disp_buf[5] & (1 << 3)))
        self.assertTrue(bool(fw.disp_buf[3] & (1 << 3)))
        # Check transaction icons are cleared
        self.assertFalse(fw.hook_en)
        self.assertFalse(fw.rs_en)

    def test_show_date_display(self):
        fw = FirmwareLcdAdapter()
        fw.appLcdShowDate(24, 10, 26)
        self.assertEqual(fw.current_screen_name, "DATE_DISPLAY")
        self.assertTrue(bool(fw.disp_buf[5] & (1 << 3)))
        self.assertTrue(bool(fw.disp_buf[3] & (1 << 3)))
        self.assertFalse(fw.hook_en)
        self.assertFalse(fw.rs_en)

    def test_rotating_idle_progression(self):
        fw = FirmwareLcdAdapter()
        s1 = fw.step_rotating_idle()
        self.assertEqual(s1, "HITACHI")
        self.assertEqual(fw.current_screen_name, "HITACHI_IDLE")

        s2 = fw.step_rotating_idle()
        self.assertEqual(s2, "TIME")
        self.assertEqual(fw.current_screen_name, "TIME_DISPLAY")

        s3 = fw.step_rotating_idle()
        self.assertEqual(s3, "DATE")
        self.assertEqual(fw.current_screen_name, "DATE_DISPLAY")

        s4 = fw.step_rotating_idle()
        self.assertEqual(s4, "HITACHI")

    def test_payment_override_takes_precedence(self):
        fw = FirmwareLcdAdapter()
        fw.step_rotating_idle() # starts on HITACHI
        
        # Payment arrives: ₹150.00
        fw.simulate_payment_received(150, 0)
        self.assertEqual(fw.current_screen_name, "PAYMENT_ACTIVE")
        self.assertTrue(fw.is_payment_active)

        # While payment is active, rotating idle must NOT overwrite the payment
        blocked_step = fw.step_rotating_idle()
        self.assertEqual(blocked_step, "PAYMENT_ACTIVE")
        self.assertEqual(fw.current_screen_name, "PAYMENT_ACTIVE")

        # After 10s idle timeout expires, it resets to HITACHI and resumes rotation
        fw.simulate_idle_timeout()
        self.assertFalse(fw.is_payment_active)
        self.assertEqual(fw.current_screen_name, "HITACHI_IDLE")

        # Next rotation advances to TIME
        next_step = fw.step_rotating_idle()
        self.assertEqual(next_step, "TIME")


if __name__ == "__main__":
    unittest.main()
