"""
Text Stress-Testing & Validation Panel for SoundBox LCDs
Executes automated test cases against 7-segment and Graphic LCD layout rules:
- Length boundaries
- Unsupported glyph detection
- Currency & numeric parsing
- Unicode / Hindi script compatibility
- Pixel width overflow and clipping
"""

import tkinter as tk
from tkinter import ttk
from typing import List, Dict
from font_manager import encode_7segment_string, GraphicFontMetrics

# Pre-defined test cases covering real SoundBox inputs and edge cases
DEFAULT_TEST_CASES = [
    # 7-Segment Test Cases
    {"text": "HITACHI", "target": "7-Segment", "desc": "Default idle firmware text (spans 8 digits)"},
    {"text": "LIT", "target": "7-Segment", "desc": "Brand screen text (3 digits)"},
    {"text": "1590.00", "target": "7-Segment", "desc": "Standard UPI payment with decimal"},
    {"text": "0.01", "target": "7-Segment", "desc": "Minimum transaction amount"},
    {"text": "99999999", "target": "7-Segment", "desc": "Maximum 8-digit capacity boundary"},
    {"text": "100000000", "target": "7-Segment", "desc": "Overflow: 9 digits (> 8 digit hardware limit)"},
    {"text": "ERROR", "target": "7-Segment", "desc": "System error message (5 digits)"},
    {"text": "HELP", "target": "7-Segment", "desc": "System prompt (4 digits)"},
    {"text": "bAt-LO", "target": "7-Segment", "desc": "Low battery indicator"},
    {"text": "WIFI", "target": "7-Segment", "desc": "Unsupported: 'W' cannot be drawn on 7-segment"},
    {"text": "KM-MAX", "target": "7-Segment", "desc": "Unsupported: 'K', 'M', 'X' unsupported on 7-seg"},
    {"text": "   100  ", "target": "7-Segment", "desc": "Leading and trailing spaces"},
    {"text": "", "target": "7-Segment", "desc": "Empty string handling"},
    
    # Graphic LCD Test Cases
    {"text": "₹ 1,590.00", "target": "Graphic 240x240", "desc": "UPI currency label with Rupee glyph"},
    {"text": "Txn: 415857764147", "target": "Graphic 240x240", "desc": "Full 12-digit transaction reference"},
    {"text": "Payment of Rs 1590 received successfully on Paytm Soundbox", "target": "Graphic 240x240", "desc": "Long single-line string (Overflow check)"},
    {"text": "भुगतान सफल हुआ", "target": "Graphic 240x240", "desc": "Hindi text script (Font asset dependency check)"},
    {"text": "Line 1: Status OK\nLine 2: Connected\nLine 3: 4G VoLTE", "target": "Graphic 240x240", "desc": "Multiline message formatting"}
]


class StressTesterPanel(ttk.Frame):
    """GUI Panel for running and viewing text stress test suites."""

    def __init__(self, master, canvas=None, **kwargs):
        super().__init__(master, **kwargs)
        self.canvas = canvas
        self.test_cases = list(DEFAULT_TEST_CASES)

        self._build_ui()
        self.run_all_tests()

    def _build_ui(self):
        # Top Controls: Run All, Add Custom, Clear
        top_bar = ttk.Frame(self)
        top_bar.pack(fill=tk.X, padx=4, pady=4)

        ttk.Button(top_bar, text="▶ Run All Stress Tests", command=self.run_all_tests).pack(side=tk.LEFT, padx=2)
        ttk.Button(top_bar, text="Export Test Report", command=self.export_report).pack(side=tk.LEFT, padx=2)

        # Custom text tester
        add_frame = ttk.LabelFrame(self, text=" Test Custom String ", padding=4)
        add_frame.pack(fill=tk.X, padx=4, pady=4)

        ttk.Label(add_frame, text="String:").pack(side=tk.LEFT, padx=(0, 2))
        self.entry_custom = ttk.Entry(add_frame, width=20)
        self.entry_custom.insert(0, "Test 12345")
        self.entry_custom.pack(side=tk.LEFT, padx=2)

        ttk.Label(add_frame, text="Target:").pack(side=tk.LEFT, padx=(6, 2))
        self.combo_target = ttk.Combobox(add_frame, values=["7-Segment", "Graphic 240x240"], width=14)
        self.combo_target.set("7-Segment")
        self.combo_target.pack(side=tk.LEFT, padx=2)

        ttk.Button(add_frame, text="Add & Test", command=self._add_custom_test).pack(side=tk.LEFT, padx=4)

        # Results Summary Banner
        self.lbl_summary = ttk.Label(self, text="Results: Running tests...", font=("Arial", 9, "bold"))
        self.lbl_summary.pack(anchor="w", padx=6, pady=2)

        # Results Table
        cols = ("target", "text", "metrics", "overflow", "glyphs", "status")
        self.tree = ttk.Treeview(self, columns=cols, show="headings", height=12)
        self.tree.heading("target", text="Display Target")
        self.tree.heading("text", text="Input String")
        self.tree.heading("metrics", text="Length / Size")
        self.tree.heading("overflow", text="Boundary / Overflow")
        self.tree.heading("glyphs", text="Glyph Support")
        self.tree.heading("status", text="Result")

        self.tree.column("target", width=110, anchor="center")
        self.tree.column("text", width=160, anchor="w")
        self.tree.column("metrics", width=100, anchor="center")
        self.tree.column("overflow", width=130, anchor="w")
        self.tree.column("glyphs", width=130, anchor="w")
        self.tree.column("status", width=75, anchor="center")

        scroll = ttk.Scrollbar(self, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)

    def _add_custom_test(self):
        text = self.entry_custom.get()
        target = self.combo_target.get()
        if text:
            self.test_cases.append({"text": text, "target": target, "desc": "User custom test case"})
            self.run_all_tests()

    def run_all_tests(self):
        """Executes all test cases and populates the results table."""
        self.tree.delete(*self.tree.get_children())
        pass_count = 0
        warn_count = 0
        fail_count = 0

        for case in self.test_cases:
            text = case["text"]
            target = case["target"]
            res = self._evaluate_case(text, target)

            if res["status"] == "PASS":
                pass_count += 1
            elif res["status"] == "WARN":
                warn_count += 1
            else:
                fail_count += 1

            self.tree.insert(
                "", tk.END,
                values=(target, text if text else "<empty>", res["metrics"], res["overflow"], res["glyphs"], res["status"])
            )

        total = len(self.test_cases)
        self.lbl_summary.config(
            text=f"Test Suite Summary: {total} Tests | ✔ {pass_count} Passed | ⚠️ {warn_count} Warnings | ❌ {fail_count} Failed",
            foreground="#20EE44" if fail_count == 0 else "#FF4444"
        )

    def _evaluate_case(self, text: str, target: str) -> Dict:
        if target == "7-Segment":
            # 7-Segment hardware checks: Max 8 physical digits, character validity
            clean_text = text.replace('.', '')
            digit_count = len(clean_text)
            encoded = encode_7segment_string(text, max_digits=8)

            unsupported = [item['char'] for item in encoded if not item.get('supported', False)]
            approximated = [item['char'] for item in encoded if item.get('approximated', False)]

            overflow = digit_count > 8
            metrics = f"{digit_count}/8 Digits"

            if overflow:
                overflow_str = f"❌ Exceeds 8 ({digit_count} digits)"
            else:
                overflow_str = "✔ Within 8 digits"

            if unsupported:
                glyph_str = f"❌ Unsupported: {','.join(unsupported[:3])}"
            elif approximated:
                glyph_str = f"⚠️ Approx: {','.join(approximated[:3])}"
            else:
                glyph_str = "✔ Native 7-Segment"

            # Determine overall status
            if overflow or unsupported:
                status = "FAIL"
            elif approximated:
                status = "WARN"
            else:
                status = "PASS"

            return {
                "metrics": metrics,
                "overflow": overflow_str,
                "glyphs": glyph_str,
                "status": status
            }

        else:
            # Graphic LCD (240x240) checks
            w, h = GraphicFontMetrics.measure_text(text, "Arial", 12)
            lines = text.split('\n')
            metrics = f"{w}x{h} px ({len(lines)} lines)"

            overflow = w > 240 or h > 240
            overflow_str = f"❌ Clips by {w-240}px" if w > 240 else "✔ Fits in 240px"

            # Check for non-ASCII / Devanagari script
            has_unicode = any(ord(c) > 127 for c in text)
            if has_unicode:
                glyph_str = "⚠️ Requires TTF/Devanagari Font Asset"
                status = "WARN" if not overflow else "FAIL"
            else:
                glyph_str = "✔ ASCII Font"
                status = "FAIL" if overflow else "PASS"

            return {
                "metrics": metrics,
                "overflow": overflow_str,
                "glyphs": glyph_str,
                "status": status
            }

    def export_report(self) -> str:
        """Generates a text report of all test cases."""
        lines = [
            "# SoundBox LCD Text Stress-Test Report",
            f"Total Tests: {len(self.test_cases)}\n",
            "| Target | Input Text | Metrics | Boundary / Overflow | Glyphs | Result |",
            "|---|---|---|---|---|---|"
        ]
        for case in self.test_cases:
            res = self._evaluate_case(case["text"], case["target"])
            lines.append(f"| {case['target']} | `{case['text']}` | {res['metrics']} | {res['overflow']} | {res['glyphs']} | **{res['status']}** |")
        
        return "\n".join(lines)
