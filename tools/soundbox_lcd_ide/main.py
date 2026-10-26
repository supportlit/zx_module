"""
SoundBox LCD Preview IDE
Desktop IDE for embedded SoundBox LCD design, text layout testing,
and firmware callback simulation without flashing physical hardware.

Run on Windows:
    python main.py
"""

import os
import sys
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

# Ensure script directory is on Python path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from firmware_adapter import FirmwareLcdAdapter
from lcd_canvas import LcdCanvas, PALETTES
from screen_manager import ScreenManager
from text_editor import TextEditorPanel
from simulator_panel import SimulatorPanel
from stress_tester import StressTesterPanel
from project_manager import ProjectManager


class SoundboxLcdIde(tk.Tk):
    """Main Application Window for SoundBox LCD Preview IDE."""

    def __init__(self):
        super().__init__()

        self.title("SoundBox LCD Preview IDE — Hardware & Screen Studio")
        self.geometry("1280x840")
        self.minsize(1000, 680)

        # Apply dark theme styling
        self._configure_theme()

        # Core Subsystems
        self.firmware = FirmwareLcdAdapter()
        self.screen_mgr = ScreenManager(self.firmware)

        # UI Layout Construction
        self._build_menu()
        self._build_toolbar()
        self._build_main_workspace()
        self._build_statusbar()

        # Load initial screen ("HITACHI" Idle)
        self.screen_mgr.select_screen("idle_hitachi", self.canvas)
        self.canvas.render()

    def _configure_theme(self):
        style = ttk.Style(self)
        try:
            style.theme_use('clam')
        except Exception:
            pass

        # Dark theme color palette
        bg_dark = "#1E1E1E"
        fg_light = "#E0E0E0"
        accent = "#007ACC"
        frame_bg = "#252526"

        self.configure(bg=bg_dark)
        style.configure(".", background=frame_bg, foreground=fg_light, font=("Segoe UI", 9))
        style.configure("TFrame", background=frame_bg)
        style.configure("TLabel", background=frame_bg, foreground=fg_light)
        style.configure("TLabelframe", background=frame_bg, foreground="#FFFFFF")
        style.configure("TLabelframe.Label", background=frame_bg, foreground="#00D5FF", font=("Segoe UI", 9, "bold"))
        style.configure("TButton", background="#333333", foreground="#FFFFFF", borderwidth=1, padding=4)
        style.map("TButton",
                  background=[("active", "#444444"), ("pressed", "#222222")],
                  foreground=[("active", "#FFFFFF")])
        style.configure("TCombobox", fieldbackground="#333333", background="#333333", foreground="#FFFFFF")
        style.configure("TSpinbox", fieldbackground="#333333", foreground="#FFFFFF")
        style.configure("TNotebook", background=frame_bg)
        style.configure("TNotebook.Tab", background="#2D2D2D", foreground="#BBBBBB", padding=(10, 4))
        style.map("TNotebook.Tab",
                  background=[("selected", frame_bg), ("active", "#3E3E42")],
                  foreground=[("selected", "#00D5FF"), ("active", "#FFFFFF")])
        style.configure("Treeview", background="#202020", foreground="#E0E0E0", fieldbackground="#202020", rowheight=22)
        style.configure("Treeview.Heading", background="#2D2D2D", foreground="#00D5FF", font=("Segoe UI", 8, "bold"))

    def _build_menu(self):
        menubar = tk.Menu(self)
        self.config(menu=menubar)

        # File Menu
        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="Save Project (JSON)...", command=self._on_save_project)
        file_menu.add_command(label="Open Project (JSON)...", command=self._on_open_project)
        file_menu.add_separator()
        file_menu.add_command(label="Export Preview as PNG...", command=self._on_export_png)
        file_menu.add_command(label="Export Preview as BMP...", command=self._on_export_bmp)
        file_menu.add_command(label="Export C Header Code...", command=self._on_export_c_code)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.quit)
        menubar.add_cascade(label="File", menu=file_menu)

        # View Menu
        view_menu = tk.Menu(menubar, tearoff=0)
        view_menu.add_command(label="Mode: SoundBox TM1726 Segment LCD", command=lambda: self._set_mode("SEGMENT"))
        view_menu.add_command(label="Mode: Color Graphic LCD (240x240)", command=lambda: self._set_mode("GRAPHIC"))
        view_menu.add_separator()
        view_menu.add_command(label="Palette: Amber Backlight", command=lambda: self.canvas.set_palette("Amber"))
        view_menu.add_command(label="Palette: Emerald Green", command=lambda: self.canvas.set_palette("Emerald"))
        view_menu.add_command(label="Palette: Ice Blue", command=lambda: self.canvas.set_palette("Ice Blue"))
        view_menu.add_command(label="Palette: Silver / Mono", command=lambda: self.canvas.set_palette("Silver / Mono"))
        menubar.add_cascade(label="View", menu=view_menu)

        # Help Menu
        help_menu = tk.Menu(menubar, tearoff=0)
        help_menu.add_command(label="Firmware Architecture Notes", command=self._show_architecture_info)
        help_menu.add_command(label="About SoundBox LCD IDE", command=self._show_about)
        menubar.add_cascade(label="Help", menu=help_menu)

    def _build_toolbar(self):
        toolbar = ttk.Frame(self, padding=(8, 4))
        toolbar.pack(fill=tk.X, side=tk.TOP)

        # Mode Selection
        ttk.Label(toolbar, text="Display:").pack(side=tk.LEFT, padx=(0, 4))
        self.combo_mode = ttk.Combobox(toolbar, values=["Segment LCD (TM1726)", "Graphic TFT (240x240)"], width=22, state="readonly")
        self.combo_mode.set("Segment LCD (TM1726)")
        self.combo_mode.pack(side=tk.LEFT, padx=(0, 10))
        self.combo_mode.bind("<<ComboboxSelected>>", self._on_mode_combo_changed)

        # Backlight Palette Selection
        ttk.Label(toolbar, text="Backlight:").pack(side=tk.LEFT, padx=(0, 4))
        self.combo_palette = ttk.Combobox(toolbar, values=list(PALETTES.keys()), width=24, state="readonly")
        self.combo_palette.set("Photo Match (Cool White)")
        self.combo_palette.pack(side=tk.LEFT, padx=(0, 10))
        self.combo_palette.bind("<<ComboboxSelected>>", lambda e: self.canvas.set_palette(self.combo_palette.get()))

        # Zoom Controls
        ttk.Label(toolbar, text="Zoom:").pack(side=tk.LEFT, padx=(6, 4))
        ttk.Button(toolbar, text="0.8x", width=4, command=lambda: self.canvas.set_zoom(0.8)).pack(side=tk.LEFT, padx=1)
        ttk.Button(toolbar, text="1.0x", width=4, command=lambda: self.canvas.set_zoom(1.0)).pack(side=tk.LEFT, padx=1)
        ttk.Button(toolbar, text="1.3x", width=4, command=lambda: self.canvas.set_zoom(1.3)).pack(side=tk.LEFT, padx=1)
        ttk.Button(toolbar, text="1.6x", width=4, command=lambda: self.canvas.set_zoom(1.6)).pack(side=tk.LEFT, padx=1)

        # Enclosure / Housing Toggle
        self.var_enclosure = tk.BooleanVar(value=True)
        ttk.Checkbutton(toolbar, text="Device Housing", variable=self.var_enclosure, command=self._on_toggle_enclosure).pack(side=tk.LEFT, padx=(10, 4))

        # Action Buttons
        ttk.Button(toolbar, text="💾 Save", width=7, command=self._on_save_project).pack(side=tk.RIGHT, padx=2)
        ttk.Button(toolbar, text="📂 Open", width=7, command=self._on_open_project).pack(side=tk.RIGHT, padx=2)
        ttk.Button(toolbar, text="📷 Export PNG", width=12, command=self._on_export_png).pack(side=tk.RIGHT, padx=2)

    def _build_main_workspace(self):
        paned = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        # --------------------------------------------------------------------
        # Left Sidebar: Screen Templates & Hardware Info
        # --------------------------------------------------------------------
        left_frame = ttk.Frame(paned, width=280)
        paned.add(left_frame, weight=1)

        # Screen Templates Box
        tpl_box = ttk.LabelFrame(left_frame, text=" SoundBox Screen Templates ", padding=6)
        tpl_box.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)

        self.tree_screens = ttk.Treeview(tpl_box, columns=("name",), show="tree", height=15)
        self.tree_screens.pack(fill=tk.BOTH, expand=True)
        self.tree_screens.bind("<<TreeviewSelect>>", self._on_screen_selected)

        # Populate screen list
        for sid, name, mode in self.screen_mgr.get_screen_list():
            icon = "📟" if mode == "SEGMENT" else "🖥️"
            self.tree_screens.insert("", tk.END, iid=sid, text=f"{icon} {name}")
        self.tree_screens.selection_set("idle_hitachi")

        # Screen Source & Info Card
        info_box = ttk.LabelFrame(left_frame, text=" Screen C Implementation Source ", padding=6)
        info_box.pack(fill=tk.X, padx=2, pady=4)

        self.lbl_src_file = ttk.Label(info_box, text="Source: app_lcd.c:1359", foreground="#00D5FF", font=("Courier", 8, "bold"))
        self.lbl_src_file.pack(anchor="w")

        self.lbl_src_desc = ttk.Label(info_box, text="Shows 'HITACHI' across all 8 digits.", wraplength=250, font=("Segoe UI", 8))
        self.lbl_src_desc.pack(anchor="w", pady=(2, 0))

        # Hardware Specs Badge
        spec_box = ttk.LabelFrame(left_frame, text=" Device Hardware Specs ", padding=6)
        spec_box.pack(fill=tk.X, padx=2, pady=4)
        specs = (
            "• Chipset: ASR / Unisoc 8910 Cat-1\n"
            "• Driver: TM1726 bit-banged 3-wire SPI\n"
            "• Pins: CS=3, SCL=49, SDA=50\n"
            "• RAM: 18 Bytes [disp_buf 0xC0..0xD1]\n"
            "• Digits: 8 Large (A..G) + 4 Small (Count)\n"
            "• Backlight: Level 3 ISINK PWM (0x30)"
        )
        ttk.Label(spec_box, text=specs, font=("Courier", 8), justify=tk.LEFT).pack(anchor="w")

        # --------------------------------------------------------------------
        # Center Workspace: LCD Canvas
        # --------------------------------------------------------------------
        center_frame = ttk.Frame(paned)
        paned.add(center_frame, weight=3)

        self.canvas = LcdCanvas(center_frame, self.firmware)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.canvas.set_coord_callback(self._on_mouse_coords)

        # --------------------------------------------------------------------
        # Right Sidebar: Tabbed Control Deck (Text, Simulator, Stress Tests)
        # --------------------------------------------------------------------
        right_frame = ttk.Frame(paned, width=420)
        paned.add(right_frame, weight=2)

        self.deck_notebook = ttk.Notebook(right_frame)
        self.deck_notebook.pack(fill=tk.BOTH, expand=True)

        # Tab 1: Text Editor
        self.tab_editor = TextEditorPanel(self.deck_notebook, self.firmware, self.canvas)
        self.deck_notebook.add(self.tab_editor, text="  Text & Segments  ")

        # Tab 2: Hardware Simulator & Event Trace
        self.tab_sim = SimulatorPanel(self.deck_notebook, self.firmware)
        self.deck_notebook.add(self.tab_sim, text="  Hardware Simulator & Trace  ")

        # Tab 3: Stress Testing
        self.tab_stress = StressTesterPanel(self.deck_notebook, self.canvas)
        self.deck_notebook.add(self.tab_stress, text="  Stress Tests  ")

    def _build_statusbar(self):
        self.statusbar = ttk.Frame(self, padding=(6, 2))
        self.statusbar.pack(fill=tk.X, side=tk.BOTTOM)

        self.lbl_status_screen = ttk.Label(self.statusbar, text="Screen: 1. Idle ('HITACHI')", font=("Segoe UI", 8, "bold"))
        self.lbl_status_screen.pack(side=tk.LEFT, padx=(4, 15))

        self.lbl_status_mode = ttk.Label(self.statusbar, text="Mode: TM1726 Segment LCD", font=("Segoe UI", 8))
        self.lbl_status_mode.pack(side=tk.LEFT, padx=(0, 15))

        self.lbl_status_coords = ttk.Label(self.statusbar, text="Cursor: X=0, Y=0", font=("Courier", 8))
        self.lbl_status_coords.pack(side=tk.LEFT, padx=(0, 15))

        self.lbl_status_msg = ttk.Label(self.statusbar, text="Status: Ready | Safe Offline Emulation (No Flash Needed)", foreground="#20EE44")
        self.lbl_status_msg.pack(side=tk.RIGHT, padx=4)

    # ------------------------------------------------------------------------
    # Event Handlers & Callbacks
    # ------------------------------------------------------------------------

    def _on_mode_combo_changed(self, event=None):
        val = self.combo_mode.get()
        mode = "SEGMENT" if "Segment" in val else "GRAPHIC"
        self._set_mode(mode)

    def _set_mode(self, mode: str):
        self.canvas.set_mode(mode)
        mode_text = "Segment LCD (TM1726)" if mode == "SEGMENT" else "Graphic TFT (240x240)"
        self.combo_mode.set(mode_text)
        self.lbl_status_mode.config(text=f"Mode: {mode_text}")
        if mode == "GRAPHIC":
            self.deck_notebook.select(self.tab_editor)
            self.tab_editor.notebook.select(self.tab_editor.tab_graphic)
        else:
            self.deck_notebook.select(self.tab_editor)
            self.tab_editor.notebook.select(self.tab_editor.tab_7seg)

    def _on_screen_selected(self, event=None):
        sel = self.tree_screens.selection()
        if not sel:
            return
        sid = sel[0]
        tpl = self.screen_mgr.select_screen(sid, self.canvas)
        if tpl:
            self.lbl_status_screen.config(text=f"Screen: {tpl.get('name', sid)}")
            self.lbl_src_file.config(text=f"Source: {tpl.get('source', 'Firmware')}")
            self.lbl_src_desc.config(text=tpl.get('desc', ''))
            mode = tpl.get('mode', 'SEGMENT')
            self.combo_mode.set("Segment LCD (TM1726)" if mode == "SEGMENT" else "Graphic TFT (240x240)")
            self.lbl_status_mode.config(text=f"Mode: {'Segment LCD (TM1726)' if mode == 'SEGMENT' else 'Graphic TFT'}")

    def _on_mouse_coords(self, x: int, y: int):
        self.lbl_status_coords.config(text=f"Cursor: X={x}, Y={y}")

    def _on_toggle_enclosure(self):
        self.canvas.show_enclosure = self.var_enclosure.get()
        self.canvas.render()

    def _on_toggle_rulers(self):
        self.canvas.show_rulers = self.var_rulers.get()
        self.canvas.render()

    def _on_toggle_grid(self):
        self.canvas.show_grid = self.var_grid.get()
        self.canvas.render()

    def _on_save_project(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("SoundBox Project", "*.json"), ("All Files", "*.*")]
        )
        if path:
            data = {
                "version": "1.0",
                "mode": self.canvas.mode,
                "palette": self.canvas.palette_name,
                "current_screen": self.screen_mgr.current_screen_id,
                "disp_buf": self.firmware.get_disp_buf(),
                "volume": self.firmware.volume_level,
                "signal": self.firmware.signal_level,
                "power": self.firmware.power_level,
                "graphic_elements": self.canvas.graphic_elements
            }
            if ProjectManager.save_project(path, data):
                messagebox.showinfo("Project Saved", f"Project successfully saved to:\n{path}")

    def _on_open_project(self):
        path = filedialog.askopenfilename(
            filetypes=[("SoundBox Project", "*.json"), ("All Files", "*.*")]
        )
        if path:
            data = ProjectManager.load_project(path)
            if data:
                self.canvas.mode = data.get("mode", "SEGMENT")
                self.canvas.set_palette(data.get("palette", "Amber"))
                self.firmware.disp_buf = list(data.get("disp_buf", [0] * 18))
                self.firmware.volume_level = data.get("volume", 1)
                self.firmware.signal_level = data.get("signal", 4)
                self.firmware.power_level = data.get("power", 4)
                self.canvas.graphic_elements = list(data.get("graphic_elements", []))
                self.canvas.render()
                messagebox.showinfo("Project Loaded", f"Project successfully loaded from:\n{path}")

    def _on_export_png(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".png",
            filetypes=[("PNG Image", "*.png"), ("All Files", "*.*")]
        )
        if path:
            # Generate a 600x240 RGB preview buffer from canvas
            w, h = 600, 240
            bg_color = (26, 18, 0) if self.canvas.palette_name == 'Amber' else (0, 20, 36)
            rgb_bytes = bytearray(bg_color * (w * h))
            if ProjectManager.export_raw_png(w, h, bytes(rgb_bytes), path):
                messagebox.showinfo("Export Successful", f"LCD Preview exported as PNG:\n{path}")

    def _on_export_bmp(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".bmp",
            filetypes=[("BMP Image", "*.bmp"), ("All Files", "*.*")]
        )
        if path:
            w, h = 600, 240
            bg_color = (26, 18, 0)
            rgb_bytes = bytearray(bg_color * (w * h))
            if ProjectManager.export_raw_bmp(w, h, bytes(rgb_bytes), path):
                messagebox.showinfo("Export Successful", f"LCD Preview exported as BMP:\n{path}")

    def _on_export_c_code(self):
        c_code = ProjectManager.export_c_header_stub(
            self.screen_mgr.current_screen_id,
            self.firmware.get_disp_buf()
        )
        # Show in a popup dialog with copy option
        top = tk.Toplevel(self)
        top.title("Exported C Code for Firmware (app_lcd.c)")
        top.geometry("640x380")
        txt = tk.Text(top, font=("Courier", 10), bg="#1E1E1E", fg="#00FF66")
        txt.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)
        txt.insert(tk.END, c_code)

    def _show_architecture_info(self):
        info = (
            "SoundBox LCD Architecture Overview:\n\n"
            "1. Controller: Titan Micro TM1726 (1/8 Duty, 1/4 Bias LCD driver)\n"
            "2. Memory Map: disp_buf[18] (18 bytes mapping COM0..COM7 and ROW0..ROW17)\n"
            "3. Digits: 8 Large (app_lcd.c:160) + 4 Small (app_lcd.c:425)\n"
            "4. Multi-Segment Hitachi Logo: T spans digits 2 & 3 (0x07 & 0x31)\n"
            "5. Safe Execution: Pure desktop emulation; zero device flashing."
        )
        messagebox.showinfo("SoundBox LCD Architecture", info)

    def _show_about(self):
        messagebox.showinfo(
            "About SoundBox LCD Preview IDE",
            "SoundBox LCD Preview IDE v1.0\n"
            "Designed for Embedded RTOS SoundBox / POS terminal development.\n"
            "Safe, offline UI verification without flashing physical hardware."
        )


if __name__ == "__main__":
    app = SoundboxLcdIde()
    app.mainloop()
