"""
Interactive Firmware Simulator & Event Trace Panel
Enables:
1. Simulating physical keys (Volume Up, Volume Down, Function Key, Power Key).
2. Simulating network and accessory events (VBUS charger plug-in, SIM removal, MQTT payment message).
3. Real-time audit log of mapped C firmware callbacks and state changes.
4. Live TM1726 disp_buf[18] register memory inspector.
"""

import tkinter as tk
from tkinter import ttk
from typing import Dict, List
from firmware_adapter import FirmwareLcdAdapter

class SimulatorPanel(ttk.Frame):
    """Interactive hardware event trigger and firmware callback tracer."""

    def __init__(self, master, firmware: FirmwareLcdAdapter, **kwargs):
        super().__init__(master, **kwargs)
        self.firmware = firmware
        self.firmware.add_listener(self._update_views)

        self._build_ui()

    def _build_ui(self):
        # 3 Sections: Hardware Triggers, Event Audit Log, TM1726 Register Viewer
        paned = ttk.PanedWindow(self, orient=tk.VERTICAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        # 1. Hardware Buttons & Simulated Events Box
        top_frame = ttk.LabelFrame(paned, text=" Simulated Hardware Inputs & Events ", padding=6)
        paned.add(top_frame, weight=1)

        # Button row 1: Keys
        k_row = ttk.Frame(top_frame)
        k_row.pack(fill=tk.X, pady=2)
        ttk.Label(k_row, text="Physical Buttons:").pack(side=tk.LEFT, padx=(0, 6))

        ttk.Button(k_row, text="Vol+ (Pin 33)", command=self._on_vol_up).pack(side=tk.LEFT, padx=3)
        ttk.Button(k_row, text="Vol- (Pin 34)", command=self._on_vol_down).pack(side=tk.LEFT, padx=3)
        ttk.Button(k_row, text="Func Key (Replay)", command=self._on_replay_key).pack(side=tk.LEFT, padx=3)
        ttk.Button(k_row, text="VBUS Charger", command=self._on_charger_toggle).pack(side=tk.LEFT, padx=3)

        # Event row 2: Network & Payment
        n_row = ttk.Frame(top_frame)
        n_row.pack(fill=tk.X, pady=(6, 2))
        ttk.Label(n_row, text="Network Events:").pack(side=tk.LEFT, padx=(0, 6))

        ttk.Button(n_row, text="Net Connect", command=lambda: self.firmware.simulate_network_status('CONNECTED')).pack(side=tk.LEFT, padx=2)
        ttk.Button(n_row, text="Net Lost", command=lambda: self.firmware.simulate_network_status('DISCONNECTED')).pack(side=tk.LEFT, padx=2)
        ttk.Button(n_row, text="No SIM", command=lambda: self.firmware.simulate_network_status('NO_SIM')).pack(side=tk.LEFT, padx=2)
        ttk.Button(n_row, text="No Signal", command=lambda: self.firmware.simulate_network_status('NO_SIGNAL')).pack(side=tk.LEFT, padx=2)

        # Payment trigger
        p_row = ttk.Frame(top_frame)
        p_row.pack(fill=tk.X, pady=(6, 2))
        ttk.Label(p_row, text="MQTT Payment:").pack(side=tk.LEFT, padx=(0, 6))
        self.entry_pay_amt = ttk.Entry(p_row, width=8)
        self.entry_pay_amt.insert(0, "150.00")
        self.entry_pay_amt.pack(side=tk.LEFT, padx=2)

        ttk.Button(p_row, text="Trigger MQTT Receive", command=self._on_trigger_payment).pack(side=tk.LEFT, padx=4)
        ttk.Button(p_row, text="10s Idle Timeout", command=self._on_idle_timeout).pack(side=tk.LEFT, padx=4)

        # Event row 3: Rotating Idle Screen Controls (5s cycle)
        r_row = ttk.Frame(top_frame)
        r_row.pack(fill=tk.X, pady=(6, 2))
        ttk.Label(r_row, text="Rotating Idle:").pack(side=tk.LEFT, padx=(0, 6))

        self.rotating_timer_id = None
        self.btn_rotate_toggle = ttk.Button(r_row, text="▶ Start 5s Auto-Rotation", command=self._toggle_rotation)
        self.btn_rotate_toggle.pack(side=tk.LEFT, padx=3)

        ttk.Button(r_row, text="Step Next ➜", command=self._on_step_rotation).pack(side=tk.LEFT, padx=3)

        self.lbl_rotate_stage = ttk.Label(r_row, text="Stage: Idle (HITACHI)", font=("Arial", 9, "bold"))
        self.lbl_rotate_stage.pack(side=tk.LEFT, padx=8)

        # 2. Event Audit Log (Firmware callback tracer)
        mid_frame = ttk.LabelFrame(paned, text=" C Firmware Callback & State Trace Log ", padding=6)
        paned.add(mid_frame, weight=2)

        cols = ("time", "event", "handler", "desc")
        self.tree_log = ttk.Treeview(mid_frame, columns=cols, show="headings", height=6)
        self.tree_log.heading("time", text="Time")
        self.tree_log.heading("event", text="Event ID")
        self.tree_log.heading("handler", text="C Handler Function")
        self.tree_log.heading("desc", text="Action / Condition / State Changes")

        self.tree_log.column("time", width=65, anchor="center")
        self.tree_log.column("event", width=120, anchor="w")
        self.tree_log.column("handler", width=220, anchor="w")
        self.tree_log.column("desc", width=260, anchor="w")

        scroll_log = ttk.Scrollbar(mid_frame, orient=tk.VERTICAL, command=self.tree_log.yview)
        self.tree_log.configure(yscrollcommand=scroll_log.set)
        self.tree_log.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll_log.pack(side=tk.RIGHT, fill=tk.Y)

        # 3. TM1726 Memory Register Viewer
        bot_frame = ttk.LabelFrame(paned, text=" TM1726 RAM Inspector (disp_buf[18] Register Dump) ", padding=6)
        paned.add(bot_frame, weight=1)

        self.txt_registers = tk.Text(bot_frame, height=4, bg="#111111", fg="#00FF66", font=("Courier", 9))
        self.txt_registers.pack(fill=tk.BOTH, expand=True)

        self._update_views()

    def _on_vol_up(self):
        self.firmware.simulate_key_vol_up()

    def _on_vol_down(self):
        self.firmware.simulate_key_vol_down()

    def _on_replay_key(self):
        if self.firmware.last_payment_amount > 0:
            rupees = int(self.firmware.last_payment_amount)
            paise = int(round((self.firmware.last_payment_amount - rupees) * 100))
            self.firmware.simulate_payment_received(rupees, paise, is_replay=True)
            self.firmware.log_event(
                "KEY_REPLAY",
                "customer_payment_replay_last()",
                f"Replayed previous payment ₹{self.firmware.last_payment_amount:.2f} (Txn count retained: {self.firmware.total_txn_count})"
            )
        else:
            self.firmware.log_event(
                "KEY_REPLAY_EMPTY",
                "customer_payment_replay_last()",
                "Replay requested but no previous transaction exists in memory"
            )

    def _on_charger_toggle(self):
        self.firmware.appLcdenablecharger()

    def _toggle_rotation(self):
        if self.rotating_timer_id is not None:
            self.after_cancel(self.rotating_timer_id)
            self.rotating_timer_id = None
            self.btn_rotate_toggle.config(text="▶ Start 5s Auto-Rotation")
            self.lbl_rotate_stage.config(text="Stage: Paused")
            self.firmware.log_event("ROTATE_PAUSE", "simulator", "Rotating idle screen paused")
        else:
            self.btn_rotate_toggle.config(text="⏸ Pause Auto-Rotation")
            self.firmware.log_event("ROTATE_START", "customer_lcd_start_rotating_idle()", "Starting 5s rotating idle screen")
            self._on_step_rotation()
            self._schedule_rotation_tick()

    def _schedule_rotation_tick(self):
        if self.rotating_timer_id is not None:
            self.after_cancel(self.rotating_timer_id)
        # Schedule next tick in 5000ms (5 seconds)
        self.rotating_timer_id = self.after(5000, self._rotation_tick)

    def _rotation_tick(self):
        self._on_step_rotation()
        self._schedule_rotation_tick()

    def _on_step_rotation(self):
        stage_name = self.firmware.step_rotating_idle()
        stage_text = {
            "HITACHI": "Stage: 1/3 HITACHI (Brand)",
            "TIME": "Stage: 2/3 TIME (Local Clock)",
            "DATE": "Stage: 3/3 DATE (Calendar)",
            "PAYMENT_ACTIVE": "Stage: PAYMENT OVERRIDE (Paused)"
        }.get(stage_name, f"Stage: {stage_name}")
        self.lbl_rotate_stage.config(text=stage_text)

    def _on_trigger_payment(self):
        amt_str = self.entry_pay_amt.get().strip()
        rupees = 150
        paise = 0
        try:
            parts = amt_str.split('.')
            rupees = int(parts[0])
            if len(parts) > 1:
                paise = int(parts[1][:2])
        except Exception:
            rupees = 150
        self.firmware.simulate_payment_received(rupees, paise)
        self.lbl_rotate_stage.config(text="Stage: PAYMENT ACTIVE (10s Override)")

    def _on_idle_timeout(self):
        self.firmware.is_payment_active = False
        self.firmware.rotating_idle_stage = 0
        self.firmware.appLcdShowTextHitachi()
        self.lbl_rotate_stage.config(text="Stage: 1/3 HITACHI (Resumed)")

    def _update_views(self):
        # Update Event Log
        self.tree_log.delete(*self.tree_log.get_children())
        for entry in reversed(self.firmware.event_log[-30:]):
            self.tree_log.insert(
                "", tk.END,
                values=(entry['timestamp'], entry['event_id'], entry['handler'], entry['desc'])
            )

        # Update Register Dump
        disp_buf = self.firmware.get_disp_buf()
        hex_dump = " ".join(f"{b:02X}" for b in disp_buf)
        ascii_map = ""
        for idx, b in enumerate(disp_buf):
            ascii_map += f"R{idx:02d}: 0x{b:02X} (COM: {b:08b})  "
            if (idx + 1) % 3 == 0:
                ascii_map += "\n"

        self.txt_registers.delete("1.0", tk.END)
        header = f"TM1726 Memory [0xC0 - 0xD1] (Total 18 Bytes):\nRaw: {hex_dump}\n\n"
        self.txt_registers.insert(tk.END, header + ascii_map)
