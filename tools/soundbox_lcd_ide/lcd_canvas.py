"""
SoundBox LCD Preview Canvas — Photo-Matched Realistic Hardware Renderer
Replicates the exact physical SoundBox device seen in hardware photos:
1. Deep matte blue/slate enclosure with white top trim.
2. Square glossy display window with deep pitch-black OLED LCD glass.
3. Crisp cool-white illuminated segments and status icons.
4. Top row: Cellular signal tower + bars, speaker icon, battery icon with level bars.
5. Middle row: 8 large 7-segment digits (H I 7 ┌ A C H 1).
6. Bottom row: "Day Count" illuminated label on bottom-left, numeric count on bottom-right.
7. Interactive tactile buttons directly on the device casing (Power, Vol-, Vol+, Paytm).
"""

import tkinter as tk
from typing import Dict, List, Tuple, Optional
from firmware_adapter import ROW, COM, FirmwareLcdAdapter

# Color Palettes with Photo-Matched Default
PALETTES = {
    'Photo Match (Cool White)': {
        'bg': '#07090D',          # Deep pitch-black LCD glass
        'lit': '#F0F6FF',         # Crisp, bright cool-white segment glow
        'ghost': '#121822',       # Subtle unlit segment trace
        'housing': '#243447',     # Photo-matched matte dark-blue terminal body
        'housing_top': '#ECECE6', # Cream/white top rounded lip
        'bezel': '#0A0C10',       # Glossy screen bezel
        'btn_bg': '#1B2736',      # Circular button background
        'btn_fg': '#C5D4E3',      # Button icon symbol color
        'border': '#151F2C',
        'text': '#F0F6FF'
    },
    'Amber': {
        'bg': '#140D00',
        'lit': '#FFA200',
        'ghost': '#332000',
        'housing': '#222224',
        'housing_top': '#ECECE6',
        'bezel': '#0C0C0E',
        'btn_bg': '#2A2A2E',
        'btn_fg': '#FFA200',
        'border': '#442800',
        'text': '#FFA200'
    },
    'Emerald': {
        'bg': '#001406',
        'lit': '#20EE44',
        'ghost': '#0A3315',
        'housing': '#1E2420',
        'housing_top': '#EAEAE0',
        'bezel': '#0B0F0C',
        'btn_bg': '#222B24',
        'btn_fg': '#20EE44',
        'border': '#0E441B',
        'text': '#20EE44'
    },
    'Ice Blue': {
        'bg': '#00111F',
        'lit': '#00D5FF',
        'ghost': '#002E50',
        'housing': '#1E2C3D',
        'housing_top': '#ECECE6',
        'bezel': '#090E16',
        'btn_bg': '#192638',
        'btn_fg': '#00D5FF',
        'border': '#003C68',
        'text': '#00D5FF'
    },
    'Silver / Mono': {
        'bg': '#95A395',
        'lit': '#111811',
        'ghost': '#808F80',
        'housing': '#303032',
        'housing_top': '#ECECE6',
        'bezel': '#18181A',
        'btn_bg': '#3A3A3D',
        'btn_fg': '#D0D0D0',
        'border': '#606E60',
        'text': '#111811'
    }
}


class LcdCanvas(tk.Frame):
    """
    Tkinter Canvas widget rendering the exact physical SoundBox terminal.
    """

    def __init__(self, master, firmware: FirmwareLcdAdapter, **kwargs):
        super().__init__(master, **kwargs)
        self.firmware = firmware
        self.firmware.add_listener(self.render)

        # Display mode: 'SEGMENT' or 'GRAPHIC'
        self.mode = 'SEGMENT'
        self.palette_name = 'Photo Match (Cool White)'
        self.zoom = 1.3
        self.show_enclosure = True
        self.show_rulers = False
        self.show_grid = False
        self.show_overflow = True

        # Interactive button bounding boxes on canvas: (x1, y1, x2, y2, callback)
        self.clickable_regions: List[Tuple[int, int, int, int, str]] = []

        # Graphic LCD dimensions
        self.graphic_width = 240
        self.graphic_height = 240
        self.graphic_elements: List[Dict] = []

        # Canvas Setup
        self.canvas = tk.Canvas(self, bg='#141416', highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.canvas.bind("<Configure>", lambda e: self.render())
        self.canvas.bind("<Motion>", self._on_mouse_move)
        self.canvas.bind("<Button-1>", self._on_canvas_click)

        # Coordinate status callback
        self.coord_callback = None

    def set_palette(self, name: str):
        if name in PALETTES:
            self.palette_name = name
            self.render()

    def set_zoom(self, zoom: float):
        self.zoom = max(0.6, min(zoom, 4.0))
        self.render()

    def set_mode(self, mode: str):
        self.mode = mode
        self.render()

    def toggle_enclosure(self):
        self.show_enclosure = not self.show_enclosure
        self.render()

    def set_coord_callback(self, cb):
        self.coord_callback = cb

    def _on_mouse_move(self, event):
        if self.coord_callback:
            self.coord_callback(event.x, event.y)

    def _on_canvas_click(self, event):
        """Detect clicks on the virtual hardware buttons below the display."""
        mx, my = event.x, event.y
        for x1, y1, x2, y2, action in self.clickable_regions:
            if x1 <= mx <= x2 and y1 <= my <= y2:
                if action == 'power':
                    self.firmware.appLcdenablecharger()
                elif action == 'vol_down':
                    self.firmware.simulate_key_vol_down()
                elif action == 'vol_up':
                    self.firmware.simulate_key_vol_up()
                elif action == 'paytm':
                    self._on_paytm_btn_click()
                return

    def _on_paytm_btn_click(self):
        if self.firmware.current_screen_name == "HITACHI_IDLE":
            # Broadcast sample payment on Paytm button click
            self.firmware.simulate_payment_received(1590, 0, txn_id="415857764147")
        else:
            # Revert to idle Hitachi screen
            self.firmware.appLcdShowTextHitachi()

    def render(self):
        """Redraws the physical SoundBox terminal or Graphic LCD."""
        self.canvas.delete("all")
        self.clickable_regions.clear()
        cw = self.canvas.winfo_width()
        ch = self.canvas.winfo_height()
        if cw <= 20 or ch <= 20:
            return

        pal = PALETTES[self.palette_name]

        if self.mode == 'SEGMENT':
            self._render_physical_soundbox(cw, ch, pal)
        else:
            self._render_graphic_lcd(cw, ch, pal)

    # ========================================================================
    # Photo-Matched SoundBox Physical Terminal Rendering
    # ========================================================================

    def _render_physical_soundbox(self, cw: int, ch: int, pal: Dict):
        # Physical terminal virtual dimensions (460 x 540)
        vw, vh = 460, 540
        sw = int(vw * self.zoom * 0.75)
        sh = int(vh * self.zoom * 0.75)

        ox = max(10, (cw - sw) // 2)
        oy = max(10, (ch - sh) // 2)
        scale = sw / vw

        if self.show_enclosure:
            self._draw_device_casing(ox, oy, sw, sh, scale, pal)
            # Position of the square LCD screen window on the device
            lw = int(280 * scale)
            lh = int(240 * scale)
            lx = ox + (sw - lw) // 2
            ly = oy + int(56 * scale)
        else:
            # Standalone screen mode
            lw = int(380 * scale * 1.1)
            lh = int(300 * scale * 1.1)
            lx = max(20, (cw - lw) // 2)
            ly = max(20, (ch - lh) // 2)

        # LCD Screen Bezel (Dark glossy surround)
        self.canvas.create_rectangle(
            lx - int(10 * scale), ly - int(10 * scale),
            lx + lw + int(10 * scale), ly + lh + int(10 * scale),
            fill=pal['bezel'], outline='#10141C', width=2
        )

        # Deep OLED Pitch-Black LCD Glass
        self.canvas.create_rectangle(
            lx, ly, lx + lw, ly + lh,
            fill=pal['bg'], outline=pal['border'], width=1
        )

        disp_buf = self.firmware.get_disp_buf()

        # Render Display Subsystems
        self._render_top_row_icons(lx, ly, lw, lh, scale, pal, disp_buf)
        self._render_center_hitachi_digits(lx, ly, lw, lh, scale, pal, disp_buf)
        self._render_bottom_day_count(lx, ly, lw, lh, scale, pal, disp_buf)

    def _draw_device_casing(self, ox: int, oy: int, sw: int, sh: int, scale: float, pal: Dict):
        """Draws the dark blue matte plastic body and buttons matching user photo."""
        # Main housing body (rounded bottom taper)
        body_poly = [
            ox + int(18 * scale), oy + int(16 * scale),             # Top-left
            ox + sw - int(18 * scale), oy + int(16 * scale),        # Top-right
            ox + sw - int(44 * scale), oy + sh - int(12 * scale),   # Bottom-right
            ox + int(44 * scale), oy + sh - int(12 * scale)         # Bottom-left
        ]
        self.canvas.create_polygon(body_poly, fill=pal['housing'], outline='#151E28', width=2)

        # White/cream rounded top lip
        self.canvas.create_oval(
            ox + int(10 * scale), oy, ox + sw - int(10 * scale), oy + int(32 * scale),
            fill=pal['housing_top'], outline='#C8C8C2', width=1
        )

        # 4 Circular Tactile Buttons below display
        btn_y = oy + int(342 * scale)
        btn_r = int(19 * scale)
        btn_spacing = int(58 * scale)
        btn_start_x = ox + sw // 2 - int(btn_spacing * 1.5)

        buttons_def = [
            ("power", "⏻", "Power / Charger"),
            ("vol_down", "🔉-", "Volume Down"),
            ("vol_up", "🔊+", "Volume Up"),
            ("paytm", "paytm", "Function / Replay")
        ]

        for i, (action, icon_text, tip) in enumerate(buttons_def):
            bx = btn_start_x + i * btn_spacing
            by = btn_y

            # Button drop shadow
            self.canvas.create_oval(
                bx - btn_r, by - btn_r + 2, bx + btn_r, by + btn_r + 2,
                fill='#101620', outline=""
            )
            # Button body
            self.canvas.create_oval(
                bx - btn_r, by - btn_r, bx + btn_r, by + btn_r,
                fill=pal['btn_bg'], outline='#2E3E50', width=int(max(1, 1.5 * scale))
            )
            # Button icon / label
            font_size = max(7, int(9 * scale)) if len(icon_text) > 2 else max(9, int(13 * scale))
            self.canvas.create_text(
                bx, by, text=icon_text, fill=pal['btn_fg'],
                font=("Arial", font_size, "bold")
            )
            # Register click target region
            self.clickable_regions.append((bx - btn_r, by - btn_r, bx + btn_r, by + btn_r, action))

        # Debossed "paytm" logo at bottom
        logo_y = oy + int(420 * scale)
        self.canvas.create_text(
            ox + sw // 2, logo_y, text="paytm",
            fill='#141D29', font=("Arial Black", max(11, int(18 * scale)))
        )
        self.canvas.create_text(
            ox + sw // 2, logo_y - 1, text="paytm",
            fill='#2A3D54', font=("Arial Black", max(11, int(18 * scale)))
        )

    # ========================================================================
    # Display Contents (Top Row, Center Digits, Bottom Day Count)
    # ========================================================================

    def _render_top_row_icons(self, lx: int, ly: int, lw: int, lh: int, scale: float, pal: Dict, disp_buf: List[int]):
        """Renders Signal on top-left, Speaker + Battery on top-right."""
        top_y = ly + int(22 * scale)

        # 1. Top-Left: Cellular Signal Tower & Bars
        sig_x = lx + int(20 * scale)
        sig_map = self.firmware.signal_flag_map
        bar_heights = [6, 10, 14, 18, 22]

        for i in range(min(5, len(sig_map))):
            r, c = ROW(sig_map[i]), COM(sig_map[i])
            is_lit = bool(disp_buf[r] & (1 << c))
            bx = sig_x + int((i * 6) * scale)
            bh = int(bar_heights[i] * scale * 0.7)
            color = pal['lit'] if is_lit else pal['ghost']
            self.canvas.create_rectangle(
                bx, top_y + int(16 * scale) - bh, bx + int(4 * scale), top_y + int(16 * scale),
                fill=color, outline=""
            )

        # Network Error Badges if active
        if self.firmware.no_sim:
            self.canvas.create_text(
                sig_x + int(36 * scale), top_y + int(10 * scale),
                text="NO SIM", fill='#FF4444', font=("Arial", max(6, int(7 * scale)), "bold")
            )
        elif self.firmware.no_signal:
            self.canvas.create_text(
                sig_x + int(48 * scale), top_y + int(10 * scale),
                text="NO SIGNAL", fill='#FF4444', font=("Arial", max(6, int(7 * scale)), "bold")
            )

        # 2. Top-Right: Speaker Icon
        spk_x = lx + lw - int(78 * scale)
        spk_y = top_y + int(8 * scale)
        spk_lit = self.firmware.volume_level > 0
        spk_color = pal['lit'] if spk_lit else pal['ghost']

        # Speaker horn polygon
        self.canvas.create_polygon([
            spk_x, spk_y + int(3 * scale),
            spk_x + int(4 * scale), spk_y + int(3 * scale),
            spk_x + int(9 * scale), spk_y - int(2 * scale),
            spk_x + int(9 * scale), spk_y + int(10 * scale),
            spk_x + int(4 * scale), spk_y + int(5 * scale),
            spk_x, spk_y + int(5 * scale)
        ], fill=spk_color, outline="")
        # Sound wave arc
        self.canvas.create_arc(
            spk_x + int(7 * scale), spk_y - int(3 * scale),
            spk_x + int(16 * scale), spk_y + int(11 * scale),
            start=-50, extent=100, style=tk.ARC, outline=spk_color, width=int(max(1, 1.5 * scale))
        )

        # 3. Top-Right: Battery Icon with Charge Bars
        bat_x = lx + lw - int(50 * scale)
        bat_y = top_y + int(2 * scale)
        bw = int(32 * scale)
        bh = int(14 * scale)

        # Battery outline
        self.canvas.create_rectangle(
            bat_x, bat_y, bat_x + bw, bat_y + bh,
            outline=pal['lit'] if self.firmware.power_level > 0 else pal['ghost'],
            width=int(max(1, 1.5 * scale))
        )
        # Battery terminal knob
        self.canvas.create_rectangle(
            bat_x + bw, bat_y + int(3 * scale), bat_x + bw + int(2.5 * scale), bat_y + bh - int(3 * scale),
            fill=pal['lit'] if self.firmware.power_level > 0 else pal['ghost'], outline=""
        )

        # Internal charge bars
        pwr_map = self.firmware.power_flag_map
        for i in range(min(4, len(pwr_map))):
            r, c = ROW(pwr_map[i]), COM(pwr_map[i])
            is_lit = bool(disp_buf[r] & (1 << c))
            bx = bat_x + int((3 + i * 7) * scale)
            color = pal['lit'] if is_lit else pal['ghost']
            self.canvas.create_rectangle(
                bx, bat_y + int(2.5 * scale), bx + int(5 * scale), bat_y + bh - int(2.5 * scale),
                fill=color, outline=""
            )

    def _render_center_hitachi_digits(self, lx: int, ly: int, lw: int, lh: int, scale: float, pal: Dict, disp_buf: List[int]):
        """Renders the 8 large digits centered on the display."""
        start_y = ly + int(76 * scale)
        digit_w = int(24 * scale)
        digit_h = int(50 * scale)
        digit_gap = int(6 * scale)
        seg_thickness = int(max(2.5, 4.0 * scale))

        total_digits_w = 8 * digit_w + 7 * digit_gap
        start_x = lx + (lw - total_digits_w) // 2

        # Payment indicators: Success Checkmark (Hook) & Rs. icon
        hook_lit = self.firmware.hook_en
        rs_lit = self.firmware.rs_en
        if hook_lit or rs_lit:
            # Shift digits slightly to the right to accommodate icons
            start_x = lx + int(36 * scale)
            if hook_lit:
                self.canvas.create_text(
                    lx + int(14 * scale), start_y + int(16 * scale),
                    text="✔", fill=pal['lit'], font=("Arial", max(10, int(20 * scale)), "bold")
                )
            if rs_lit:
                self.canvas.create_text(
                    lx + int(24 * scale), start_y + int(36 * scale),
                    text="Rs.", fill=pal['lit'], font=("Arial", max(8, int(13 * scale)), "bold")
                )

        large_map = self.firmware.large_digits_map
        point_map = self.firmware.point_flag_map

        for i in range(8):
            dx = start_x + i * (digit_w + digit_gap)
            dy = start_y

            # 7 segments
            seg_bits = []
            for j in range(7):
                r, c = ROW(large_map[i][j]), COM(large_map[i][j])
                seg_bits.append(bool(disp_buf[r] & (1 << c)))

            self._draw_tapered_7seg(dx, dy, digit_w, digit_h, seg_thickness, seg_bits, pal)

            # Decimal point for this digit
            pr, pc = ROW(point_map[i]), COM(point_map[i])
            dot_lit = bool(disp_buf[pr] & (1 << pc))
            dot_color = pal['lit'] if dot_lit else pal['ghost']
            dot_r = int(max(1.5, 2.5 * scale))
            dot_x = dx + digit_w + int(2 * scale)
            dot_y = dy + digit_h - dot_r
            self.canvas.create_oval(
                dot_x - dot_r, dot_y - dot_r, dot_x + dot_r, dot_y + dot_r,
                fill=dot_color, outline=""
            )

    def _render_bottom_day_count(self, lx: int, ly: int, lw: int, lh: int, scale: float, pal: Dict, disp_buf: List[int]):
        """
        Renders "Day Count" on the bottom-left and numeric count on bottom-right,
        matching the exact layout seen in the user's physical SoundBox photo.
        """
        bot_y = ly + int(178 * scale)

        # 1. Bottom-Left: "Day Count" illuminated text
        r, c = ROW(self.firmware.icon_count), COM(self.firmware.icon_count)
        count_en = bool(disp_buf[r] & (1 << c)) or self.firmware.count_en
        count_color = pal['lit'] if count_en else pal['ghost']

        self.canvas.create_text(
            lx + int(24 * scale), bot_y + int(12 * scale),
            text="Day Count", fill=count_color,
            font=("Segoe UI", max(9, int(15 * scale)), "bold"), anchor="w"
        )

        # 2. Bottom-Right: 4 small digits displaying count (e.g. '5')
        small_map = self.firmware.small_digits_map
        digit_w = int(14 * scale)
        digit_h = int(28 * scale)
        digit_gap = int(4 * scale)
        seg_t = int(max(1.8, 2.5 * scale))

        start_x = lx + lw - int(95 * scale)
        start_y = bot_y

        for i in range(4):
            dx = start_x + i * (digit_w + digit_gap)
            dy = start_y

            # Raw bits order: D, C, B, A, E, G, F
            raw = [bool(disp_buf[ROW(small_map[i][k])] & (1 << COM(small_map[i][k]))) for k in range(7)]
            # Reorder to A, B, C, D, E, F, G
            segs = [raw[3], raw[2], raw[1], raw[0], raw[4], raw[6], raw[5]]

            self._draw_tapered_7seg(dx, dy, digit_w, digit_h, seg_t, segs, pal)

    def _draw_tapered_7seg(self, x: int, y: int, w: int, h: int, t: int, segs: List[bool], pal: Dict):
        """
        Draws an authentic tapered 7-segment digit with unlit segment ghosting.
        segs: [A, B, C, D, E, F, G]
        """
        half_h = h // 2

        # A: Top Horizontal
        col_a = pal['lit'] if segs[0] else pal['ghost']
        self.canvas.create_polygon([
            x + t, y,
            x + w - t, y,
            x + w - 2 * t, y + t,
            x + 2 * t, y + t
        ], fill=col_a, outline="")

        # B: Top-Right Vertical
        col_b = pal['lit'] if segs[1] else pal['ghost']
        self.canvas.create_polygon([
            x + w, y + t,
            x + w, y + half_h - t // 2,
            x + w - t, y + half_h - t,
            x + w - t, y + 2 * t
        ], fill=col_b, outline="")

        # C: Bottom-Right Vertical
        col_c = pal['lit'] if segs[2] else pal['ghost']
        self.canvas.create_polygon([
            x + w, y + half_h + t // 2,
            x + w, y + h - t,
            x + w - t, y + h - 2 * t,
            x + w - t, y + half_h + t
        ], fill=col_c, outline="")

        # D: Bottom Horizontal
        col_d = pal['lit'] if segs[3] else pal['ghost']
        self.canvas.create_polygon([
            x + 2 * t, y + h - t,
            x + w - 2 * t, y + h - t,
            x + w - t, y + h,
            x + t, y + h
        ], fill=col_d, outline="")

        # E: Bottom-Left Vertical
        col_e = pal['lit'] if segs[4] else pal['ghost']
        self.canvas.create_polygon([
            x, y + half_h + t // 2,
            x + t, y + half_h + t,
            x + t, y + h - 2 * t,
            x, y + h - t
        ], fill=col_e, outline="")

        # F: Top-Left Vertical
        col_f = pal['lit'] if segs[5] else pal['ghost']
        self.canvas.create_polygon([
            x, y + t,
            x + t, y + 2 * t,
            x + t, y + half_h - t,
            x, y + half_h - t // 2
        ], fill=col_f, outline="")

        # G: Center Horizontal
        col_g = pal['lit'] if segs[6] else pal['ghost']
        self.canvas.create_polygon([
            x + t, y + half_h,
            x + 2 * t, y + half_h - t // 2,
            x + w - 2 * t, y + half_h - t // 2,
            x + w - t, y + half_h,
            x + w - 2 * t, y + half_h + t // 2,
            x + 2 * t, y + half_h + t // 2
        ], fill=col_g, outline="")

    # ========================================================================
    # Graphic Color LCD Mode (TFT 240x240)
    # ========================================================================

    def _render_graphic_lcd(self, cw: int, ch: int, pal: Dict):
        gw, gh = self.graphic_width, self.graphic_height
        sw = int(gw * self.zoom)
        sh = int(gh * self.zoom)

        ox = max(30, (cw - sw) // 2)
        oy = max(30, (ch - sh) // 2)

        # Screen bezel & background
        self.canvas.create_rectangle(
            ox - 10, oy - 10, ox + sw + 10, oy + sh + 10,
            fill='#20242B', outline='#353A42', width=2
        )
        self.canvas.create_rectangle(
            ox, oy, ox + sw, oy + sh,
            fill='#050608', outline='#444444', width=1
        )

        # Render Graphic Elements
        for elem in self.graphic_elements:
            etype = elem.get('type', 'text')
            x = ox + int(elem.get('x', 0) * self.zoom)
            y = oy + int(elem.get('y', 0) * self.zoom)
            color = elem.get('color', '#FFFFFF')

            if etype == 'text':
                text = elem.get('text', '')
                font_size = int(elem.get('font_size', 12) * self.zoom)
                font_family = elem.get('font', 'Arial')
                anchor = elem.get('anchor', 'nw')
                self.canvas.create_text(
                    x, y, text=text, fill=color,
                    font=(font_family, max(8, font_size)), anchor=anchor
                )
            elif etype == 'rect':
                w = int(elem.get('width', 50) * self.zoom)
                h = int(elem.get('height', 50) * self.zoom)
                fill = elem.get('fill', '')
                self.canvas.create_rectangle(x, y, x + w, y + h, outline=color, fill=fill)
            elif etype == 'qr_placeholder':
                size = int(elem.get('size', 100) * self.zoom)
                self.canvas.create_rectangle(x, y, x + size, y + size, fill='#FFFFFF', outline='#000000')
                self.canvas.create_text(x + size // 2, y + size // 2, text="[QR CODE]", fill='#000000', font=("Arial", 10, "bold"))
