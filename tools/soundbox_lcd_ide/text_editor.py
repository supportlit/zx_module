"""
SoundBox LCD Text Editor & Property Inspector
Provides:
1. Live text editing with immediate LCD preview update.
2. 7-Segment text translator with supported/unsupported glyph validation.
3. Graphic LCD text properties: X, Y, font, size, alignment, max width, line wrapping, clipping.
4. Real-time character count and pixel metric readouts.
"""

import tkinter as tk
from tkinter import ttk
from typing import Dict, List, Callable
from font_manager import encode_7segment_string, GraphicFontMetrics
from firmware_adapter import FirmwareLcdAdapter, ROW, COM

class TextEditorPanel(ttk.Frame):
    """Interactive property editor for LCD text and segment elements."""

    def __init__(self, master, firmware: FirmwareLcdAdapter, canvas, **kwargs):
        super().__init__(master, **kwargs)
        self.firmware = firmware
        self.canvas = canvas

        self._build_ui()

    def _build_ui(self):
        # Notebook with 2 Modes: 7-Segment Editor & Graphic Text Inspector
        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)

        self.tab_7seg = ttk.Frame(self.notebook)
        self.tab_graphic = ttk.Frame(self.notebook)

        self.notebook.add(self.tab_7seg, text="  7-Segment Text  ")
        self.notebook.add(self.tab_graphic, text="  Graphic LCD Text  ")

        self._build_7seg_tab()
        self._build_graphic_tab()

    # ========================================================================
    # 7-Segment Text Editor Tab
    # ========================================================================

    def _build_7seg_tab(self):
        frame = ttk.LabelFrame(self.tab_7seg, text=" Physical 8-Digit Display Input ", padding=8)
        frame.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        # Quick preset buttons
        preset_frame = ttk.Frame(frame)
        preset_frame.pack(fill=tk.X, pady=(0, 8))
        ttk.Label(preset_frame, text="Presets:").pack(side=tk.LEFT, padx=(0, 4))
        
        ttk.Button(preset_frame, text="HITACHI", width=8,
                   command=lambda: self._set_7seg_text("HITACHI")).pack(side=tk.LEFT, padx=2)
        ttk.Button(preset_frame, text="LIT", width=5,
                   command=lambda: self._set_7seg_text("LIT")).pack(side=tk.LEFT, padx=2)
        ttk.Button(preset_frame, text="1590.00", width=8,
                   command=lambda: self._set_7seg_text("1590.00")).pack(side=tk.LEFT, padx=2)
        ttk.Button(preset_frame, text="ERROR", width=7,
                   command=lambda: self._set_7seg_text("ERROR")).pack(side=tk.LEFT, padx=2)

        # Live Text Entry
        ttk.Label(frame, text="Type text or numbers to test on 7-segment display:").pack(anchor="w")
        self.entry_7seg = ttk.Entry(frame, font=("Courier", 13, "bold"))
        self.entry_7seg.insert(0, "HITACHI")
        self.entry_7seg.pack(fill=tk.X, pady=(2, 4))
        self.entry_7seg.bind("<KeyRelease>", self._on_7seg_key_release)

        # 'T' formatting toggle: Remove extra 'I'
        t_style_frame = ttk.Frame(frame)
        t_style_frame.pack(fill=tk.X, pady=(0, 6))
        self.var_clean_t = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            t_style_frame,
            text="Remove Extra 'I' from 'T' (Single Stem 0x07, 0x01)",
            variable=self.var_clean_t,
            command=self._on_t_style_toggle
        ).pack(side=tk.LEFT)

        # Icons Checkboxes
        icon_box = ttk.LabelFrame(frame, text=" Segment LCD Indicators ", padding=6)
        icon_box.pack(fill=tk.X, pady=6)

        self.var_hook = tk.BooleanVar(value=False)
        self.var_rs = tk.BooleanVar(value=False)
        self.var_count = tk.BooleanVar(value=False)
        self.var_paytm = tk.BooleanVar(value=False)

        row1 = ttk.Frame(icon_box)
        row1.pack(fill=tk.X)
        ttk.Checkbutton(row1, text="Success Tick (✔)", variable=self.var_hook, command=self._on_indicator_change).pack(side=tk.LEFT, padx=4)
        ttk.Checkbutton(row1, text="Rupee (Rs.)", variable=self.var_rs, command=self._on_indicator_change).pack(side=tk.LEFT, padx=4)

        row2 = ttk.Frame(icon_box)
        row2.pack(fill=tk.X, pady=(4, 0))
        ttk.Checkbutton(row2, text="COUNT Badge", variable=self.var_count, command=self._on_indicator_change).pack(side=tk.LEFT, padx=4)
        ttk.Checkbutton(row2, text="PayTm Logo", variable=self.var_paytm, command=self._on_indicator_change).pack(side=tk.LEFT, padx=4)

        # Side Digits (Txn Count amount)
        side_frame = ttk.Frame(icon_box)
        side_frame.pack(fill=tk.X, pady=(6, 0))
        ttk.Label(side_frame, text="Count Value:").pack(side=tk.LEFT, padx=(0, 4))
        self.entry_count_val = ttk.Spinbox(side_frame, from_=0, to=9999, width=6, command=self._on_indicator_change)
        self.entry_count_val.set(1)
        self.entry_count_val.pack(side=tk.LEFT)
        self.entry_count_val.bind("<KeyRelease>", lambda e: self._on_indicator_change())

        # Live Glyph Analysis Box
        self.glyph_info_frame = ttk.LabelFrame(frame, text=" 7-Segment Glyph Analysis ", padding=6)
        self.glyph_info_frame.pack(fill=tk.BOTH, expand=True, pady=6)

        self.lbl_7seg_status = ttk.Label(self.glyph_info_frame, text="Status: Exact firmware match", foreground="#20EE44")
        self.lbl_7seg_status.pack(anchor="w", pady=(0, 4))

        self.tree_glyphs = ttk.Treeview(
            self.glyph_info_frame,
            columns=("digit", "char", "hex", "support"),
            show="headings", height=8
        )
        self.tree_glyphs.heading("digit", text="Digit #")
        self.tree_glyphs.heading("char", text="Char")
        self.tree_glyphs.heading("hex", text="Segments (Hex)")
        self.tree_glyphs.heading("support", text="Validity")

        self.tree_glyphs.column("digit", width=55, anchor="center")
        self.tree_glyphs.column("char", width=45, anchor="center")
        self.tree_glyphs.column("hex", width=105, anchor="center")
        self.tree_glyphs.column("support", width=110, anchor="w")
        self.tree_glyphs.pack(fill=tk.BOTH, expand=True)

        self._update_7seg_table("HITACHI")

    def _set_7seg_text(self, text: str):
        self.entry_7seg.delete(0, tk.END)
        self.entry_7seg.insert(0, text)
        self._apply_7seg_text(text)

    def _on_7seg_key_release(self, event=None):
        text = self.entry_7seg.get()
        self._apply_7seg_text(text)

    def _apply_7seg_text(self, text: str):
        """Applies input to firmware registers and updates analysis tree."""
        # Check if text is numeric with optional decimal point (e.g. 1590.00)
        is_num = False
        val_rupees = 0
        val_paise = 0
        try:
            parts = text.split('.')
            if len(parts) <= 2 and parts[0].isdigit():
                val_rupees = int(parts[0])
                if len(parts) == 2 and parts[1].isdigit():
                    val_paise = int(parts[1][:2])
                is_num = True
        except Exception:
            is_num = False

        if is_num and text.upper() != "HITACHI" and text.upper() != "LIT":
            # Apply numeric display logic
            self.firmware.decimal = val_paise
            self.firmware.prvLcdBigNumSet(val_rupees)
            self.firmware.current_screen_name = f"AMOUNT_{val_rupees}_{val_paise}"
        elif text.upper() == "HITACHI":
            self.firmware.appLcdShowTextHitachi(remove_extra_i=self.var_clean_t.get())
        elif text.upper() == "LIT":
            self.firmware.appLcdShowTextLIT()
        else:
            # General string on 7-segment
            encoded = encode_7segment_string(text, max_digits=8, t_style="clean" if self.var_clean_t.get() else "original")
            self.firmware.prvLcdBigNumClr()
            # Clear decimal points
            for r in range(8):
                self.firmware.disp_buf[r] &= ~(1 << 3) & 0xFF

            for idx, item in enumerate(encoded):
                if idx >= 8:
                    break
                pat = item['pattern']
                for j in range(7):
                    r = ROW(self.firmware.large_digits_map[idx][j])
                    c = COM(self.firmware.large_digits_map[idx][j])
                    if pat & 0x01:
                        self.firmware.disp_buf[r] |= (1 << c)
                    else:
                        self.firmware.disp_buf[r] &= ~(1 << c) & 0xFF
                    pat >>= 1

                if item.get('has_dot', False):
                    pr = ROW(self.firmware.point_flag_map[idx])
                    pc = COM(self.firmware.point_flag_map[idx])
                    self.firmware.disp_buf[pr] |= (1 << pc)
            self.firmware.current_screen_name = "CUSTOM_7SEG"

        self._on_indicator_change()
        self._update_7seg_table(text)
        self.firmware._notify()

    def _on_t_style_toggle(self):
        self._apply_7seg_text(self.entry_7seg.get())

    def _on_indicator_change(self):
        self.firmware.Paytm_LCD_Set_Hook_En(self.var_hook.get())
        self.firmware.Paytm_LCD_Set_RS_En(self.var_rs.get())
        self.firmware.Paytm_LCD_Set_Count_En(self.var_count.get())
        self.firmware.Paytm_LCD_Set_PayTm_En(self.var_paytm.get())

        try:
            count_val = int(self.entry_count_val.get())
            if self.var_count.get():
                self.firmware.prvLcdReceiveAmountSet(count_val)
            else:
                self.firmware.prvLcdReceiveAmountClr()
        except Exception:
            pass
        self.firmware._notify()

    def _update_7seg_table(self, text: str):
        for item in self.tree_glyphs.get_children():
            self.tree_glyphs.delete(item)

        encoded = encode_7segment_string(text, max_digits=8, t_style="clean" if self.var_clean_t.get() else "original")
        unsupported_count = 0
        overflow = len(text.replace('.', '')) > 8

        for idx, item in enumerate(encoded):
            c = item['char']
            pat = item['pattern']
            has_dot = item.get('has_dot', False)
            dot_str = " + Dot" if has_dot else ""
            supp = item.get('supported', False)

            if not supp:
                unsupported_count += 1
                validity = "❌ Unsupported"
            elif item.get('approximated', False):
                validity = "⚠️ Approximated"
            elif item.get('is_special_hitachi', False):
                validity = "✔ Hitachi Multi-Seg"
            else:
                validity = "✔ Native 7-Seg"

            self.tree_glyphs.insert(
                "", tk.END,
                values=(f"Digit {idx}", c, f"0x{pat:02X}{dot_str}", validity)
            )

        if overflow:
            self.lbl_7seg_status.config(
                text=f"⚠️ Bounding Warning: Text exceeds 8 physical digits ({len(text.replace('.', ''))} > 8)!",
                foreground="#FF5555"
            )
        elif unsupported_count > 0:
            self.lbl_7seg_status.config(
                text=f"⚠️ Unsupported glyphs: {unsupported_count} character(s) cannot be formed on 7-segment!",
                foreground="#FFAA00"
            )
        else:
            self.lbl_7seg_status.config(
                text="✔ All characters supported on SoundBox LCD hardware",
                foreground="#20EE44"
            )

    # ========================================================================
    # Graphic LCD Text Editor Tab
    # ========================================================================

    def _build_graphic_tab(self):
        frame = ttk.LabelFrame(self.tab_graphic, text=" Graphic LCD Text Properties ", padding=8)
        frame.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        # Text input
        ttk.Label(frame, text="Text Content:").pack(anchor="w")
        self.entry_graphic_text = ttk.Entry(frame, font=("Arial", 11))
        self.entry_graphic_text.insert(0, "Scan to Pay: ₹ 1,590.00")
        self.entry_graphic_text.pack(fill=tk.X, pady=(2, 6))
        self.entry_graphic_text.bind("<KeyRelease>", self._on_graphic_change)

        # X & Y Coordinates
        coords_frame = ttk.Frame(frame)
        coords_frame.pack(fill=tk.X, pady=4)
        ttk.Label(coords_frame, text="X:").pack(side=tk.LEFT, padx=(0, 2))
        self.spin_x = ttk.Spinbox(coords_frame, from_=0, to=320, width=5, command=self._on_graphic_change)
        self.spin_x.set(20)
        self.spin_x.pack(side=tk.LEFT, padx=(0, 10))

        ttk.Label(coords_frame, text="Y:").pack(side=tk.LEFT, padx=(0, 2))
        self.spin_y = ttk.Spinbox(coords_frame, from_=0, to=320, width=5, command=self._on_graphic_change)
        self.spin_y.set(40)
        self.spin_y.pack(side=tk.LEFT, padx=(0, 10))

        ttk.Label(coords_frame, text="Font Size:").pack(side=tk.LEFT, padx=(0, 2))
        self.spin_size = ttk.Spinbox(coords_frame, from_=8, to=48, width=5, command=self._on_graphic_change)
        self.spin_size.set(14)
        self.spin_size.pack(side=tk.LEFT)

        # Font Family & Color
        prop_frame = ttk.Frame(frame)
        prop_frame.pack(fill=tk.X, pady=4)
        ttk.Label(prop_frame, text="Font:").pack(side=tk.LEFT, padx=(0, 2))
        self.combo_font = ttk.Combobox(prop_frame, values=["ASCII_5x7", "ASCII_8x16", "ASCII_16x24", "Arial", "Courier"], width=11)
        self.combo_font.set("Arial")
        self.combo_font.pack(side=tk.LEFT, padx=(0, 10))
        self.combo_font.bind("<<ComboboxSelected>>", self._on_graphic_change)

        ttk.Label(prop_frame, text="Color:").pack(side=tk.LEFT, padx=(0, 2))
        self.combo_color = ttk.Combobox(prop_frame, values=["#FFFFFF", "#20EE44", "#FFAA00", "#00D5FF", "#FF3333"], width=9)
        self.combo_color.set("#20EE44")
        self.combo_color.pack(side=tk.LEFT)
        self.combo_color.bind("<<ComboboxSelected>>", self._on_graphic_change)

        # Bounding Constraints
        bound_frame = ttk.LabelFrame(frame, text=" Bounding Constraints & Wrapping ", padding=6)
        bound_frame.pack(fill=tk.X, pady=6)

        b_row = ttk.Frame(bound_frame)
        b_row.pack(fill=tk.X)
        ttk.Label(b_row, text="Max Width (px):").pack(side=tk.LEFT, padx=(0, 2))
        self.spin_max_w = ttk.Spinbox(b_row, from_=20, to=320, width=5, command=self._on_graphic_change)
        self.spin_max_w.set(200)
        self.spin_max_w.pack(side=tk.LEFT, padx=(0, 10))

        ttk.Label(b_row, text="Max Lines:").pack(side=tk.LEFT, padx=(0, 2))
        self.spin_max_lines = ttk.Spinbox(b_row, from_=1, to=10, width=4, command=self._on_graphic_change)
        self.spin_max_lines.set(2)
        self.spin_max_lines.pack(side=tk.LEFT)

        # Live Measurement Readouts
        metrics_box = ttk.LabelFrame(frame, text=" Live Text Measurement ", padding=6)
        metrics_box.pack(fill=tk.BOTH, expand=True, pady=6)

        self.lbl_char_count = ttk.Label(metrics_box, text="Character Count: 25 chars")
        self.lbl_char_count.pack(anchor="w")

        self.lbl_pixel_size = ttk.Label(metrics_box, text="Measured Size: 180 x 20 px")
        self.lbl_pixel_size.pack(anchor="w")

        self.lbl_overflow_warn = ttk.Label(metrics_box, text="Status: Fits within bounding box", foreground="#20EE44")
        self.lbl_overflow_warn.pack(anchor="w", pady=(4, 0))

        # Add to canvas button
        btn_frame = ttk.Frame(frame)
        btn_frame.pack(fill=tk.X, pady=6)
        ttk.Button(btn_frame, text="Apply Text to Graphic Canvas", command=self._on_graphic_change).pack(side=tk.LEFT, fill=tk.X, expand=True)

    def _on_graphic_change(self, event=None):
        text = self.entry_graphic_text.get()
        try:
            x = int(self.spin_x.get())
            y = int(self.spin_y.get())
            size = int(self.spin_size.get())
            max_w = int(self.spin_max_w.get())
            max_lines = int(self.spin_max_lines.get())
        except ValueError:
            return

        font_name = self.combo_font.get()
        color = self.combo_color.get()

        w, h = GraphicFontMetrics.measure_text(text, font_name, size)
        overflow = (x + w > self.canvas.graphic_width) or (w > max_w)

        self.lbl_char_count.config(text=f"Character Count: {len(text)} characters")
        self.lbl_pixel_size.config(text=f"Measured Size: {w} x {h} px (Bounds: {max_w} px)")

        if overflow:
            self.lbl_overflow_warn.config(
                text=f"⚠️ CLIPPING DETECTED: Width {w}px exceeds limit {max_w}px or screen edge!",
                foreground="#FF4444"
            )
        else:
            self.lbl_overflow_warn.config(
                text="✔ Within display boundary constraints",
                foreground="#20EE44"
            )

        # Update graphic element in canvas
        elem = {
            "type": "text",
            "text": text,
            "x": x,
            "y": y,
            "font_size": size,
            "font": font_name,
            "color": color,
            "width": w,
            "height": h,
            "overflow": overflow,
            "selected": True
        }
        self.canvas.graphic_elements = [elem]
        self.canvas.render()
