"""
TASBot Atari GUI — Learnfun / Playfun launcher for Atari 2600.

Place this script alongside learnfun.exe and playfun.exe.
All paths are relative to the script's own directory.
"""

import os
import re
import sys
import time
import shutil
import subprocess
import threading
import webbrowser
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path

# Regex to strip ANSI escape sequences (colors, cursor movement, etc.)
_ANSI_RE = re.compile(r'\x1b\[[0-9;]*[A-Za-z]')

# Resolve the directory this script lives in (portable).
APP_DIR = Path(__file__).resolve().parent

LEARNFUN = APP_DIR / "learnfun.exe"
PLAYFUN = APP_DIR / "playfun.exe"
RECORDFUN = APP_DIR / "recordfun.exe"
REPLAYFUN = APP_DIR / "replayfun.exe"
CONFIG = APP_DIR / "config.txt"
STELLA_DIR = APP_DIR / "Stella"


class TASBotGUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("TASBot Atari — Learnfun / Playfun")
        self.root.resizable(True, True)
        self.root.minsize(640, 480)

        self.process: subprocess.Popen | None = None
        self.helper_procs: list[subprocess.Popen] = []
        self.stop_event = threading.Event()

        self._build_ui()
        self._check_executables()

    # ── UI construction ──────────────────────────────────────────────

    def _build_ui(self):
        # Main container
        main = ttk.Frame(self.root, padding=10)
        main.pack(fill="both", expand=True)

        # ── ROM selector ─────────────────────────────────────────────
        rom_frame = ttk.LabelFrame(main, text="ROM (.a26 / .bin)", padding=6)
        rom_frame.pack(fill="x", pady=(0, 6))

        self.rom_var = tk.StringVar()
        ttk.Entry(rom_frame, textvariable=self.rom_var, state="readonly"
                  ).pack(side="left", fill="x", expand=True, padx=(0, 6))
        ttk.Button(rom_frame, text="Browse…", command=self._browse_rom
                   ).pack(side="right")

        # ── Input log selector ────────────────────────────────────────
        movie_frame = ttk.LabelFrame(main, text="Input Log (.a26inp)", padding=6)
        movie_frame.pack(fill="x", pady=(0, 6))

        self.movie_var = tk.StringVar()
        ttk.Entry(movie_frame, textvariable=self.movie_var, state="readonly"
                  ).pack(side="left", fill="x", expand=True, padx=(0, 6))
        ttk.Button(movie_frame, text="Browse…", command=self._browse_movie
                   ).pack(side="right")

        # ── MARIONET options ──────────────────────────────────────────
        opt_frame = ttk.LabelFrame(main, text="MARIONET Options", padding=6)
        opt_frame.pack(fill="x", pady=(0, 6))

        # Row 1: helpers count
        row1 = ttk.Frame(opt_frame)
        row1.pack(fill="x", pady=(0, 4))

        ttk.Label(row1, text="Helper threads:").pack(side="left")
        self.helpers_var = tk.StringVar(value="auto")
        helpers_spin = ttk.Spinbox(
            row1, from_=1, to=128, width=5,
            textvariable=self.helpers_var)
        helpers_spin.set("auto")
        helpers_spin.pack(side="left", padx=(4, 0))
        ttk.Label(row1, text='("auto" = CPU cores \u2212 1)'
                  ).pack(side="left", padx=(6, 0))

        # Row 2: port range
        row2 = ttk.Frame(opt_frame)
        row2.pack(fill="x")

        ttk.Label(row2, text="Start port:").pack(side="left")
        self.port_var = tk.IntVar(value=29000)
        port_spin = ttk.Spinbox(
            row2, from_=1024, to=65535, width=6,
            textvariable=self.port_var)
        port_spin.pack(side="left", padx=(4, 0))
        self.port_label = ttk.Label(row2, text="")
        self.port_label.pack(side="left", padx=(6, 0))

        # Update port range preview when values change
        self.helpers_var.trace_add("write", lambda *_: self._update_port_label())
        self.port_var.trace_add("write", lambda *_: self._update_port_label())
        self._update_port_label()

        # Row 3: resume checkbox
        row3 = ttk.Frame(opt_frame)
        row3.pack(fill="x", pady=(4, 0))

        self.resume_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(row3, text="Resume from last progress file",
                        variable=self.resume_var).pack(side="left")

        # ── Action buttons ───────────────────────────────────────────
        btn_frame = ttk.Frame(main)
        btn_frame.pack(fill="x", pady=(0, 6))

        self.btn_record = ttk.Button(
            btn_frame, text="🎮  Record  (play game)",
            command=self._run_recordfun)
        self.btn_record.pack(side="left", padx=(0, 6))

        self.btn_learn = ttk.Button(
            btn_frame, text="▶  Pretrain  (learnfun)",
            command=self._run_learnfun)
        self.btn_learn.pack(side="left", padx=(0, 6))

        self.btn_play = ttk.Button(
            btn_frame, text="▶  Run  (playfun)",
            command=self._run_playfun)
        self.btn_play.pack(side="left", padx=(0, 6))

        self.btn_stop = ttk.Button(
            btn_frame, text="■  Stop", command=self._stop_process,
            state="disabled")
        self.btn_stop.pack(side="left")

        self.btn_replay = ttk.Button(
            btn_frame, text="🎬  Watch Replay",
            command=self._watch_replay)
        self.btn_replay.pack(side="left", padx=(6, 0))

        ttk.Button(
            btn_frame, text="?", width=3,
            command=lambda: webbrowser.open(
                "https://github.com/AtlasRedux/LearnfunPlayfun-Revival")
        ).pack(side="right")

        # ── Log / output area ────────────────────────────────────────
        log_frame = ttk.LabelFrame(main, text="Output", padding=4)
        log_frame.pack(fill="both", expand=True)

        self.log = tk.Text(log_frame, wrap="word", state="disabled",
                           bg="#1e1e1e", fg="#cccccc",
                           font=("Consolas", 9), insertbackground="#ccc")
        scroll = ttk.Scrollbar(log_frame, command=self.log.yview)
        self.log.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.log.pack(side="left", fill="both", expand=True)

        # Tag for error text
        self.log.tag_configure("err", foreground="#ff6b6b")
        self.log.tag_configure("info", foreground="#69db7c")

        # ── Status bar ───────────────────────────────────────────────
        self.status_var = tk.StringVar(value="Ready.")
        ttk.Label(main, textvariable=self.status_var, relief="sunken",
                  anchor="w").pack(fill="x", pady=(6, 0))

        # ── Auto-detect files already in folder ──────────────────────
        self._auto_detect()

    # ── File browsing ────────────────────────────────────────────────

    def _browse_rom(self):
        path = filedialog.askopenfilename(
            title="Select Atari 2600 ROM",
            filetypes=[("Atari 2600 ROMs", "*.a26 *.bin"),
                       ("All files", "*.*")],
            initialdir=str(APP_DIR))
        if path:
            local = self._ensure_local(path)
            if local:
                self.rom_var.set(local.name)

    def _browse_movie(self):
        path = filedialog.askopenfilename(
            title="Select Atari 2600 Input Log",
            filetypes=[("Atari input logs", "*.a26inp"),
                       ("All files", "*.*")],
            initialdir=str(APP_DIR))
        if path:
            local = self._ensure_local(path)
            if local:
                self.movie_var.set(local.name)

    def _ensure_local(self, filepath: str) -> Path | None:
        """Copy the file into APP_DIR if it isn't there already.
        Returns the local Path, or None on failure."""
        src = Path(filepath).resolve()
        dest = APP_DIR / src.name
        if src == dest:
            return dest
        if dest.exists():
            overwrite = messagebox.askyesno(
                "File exists",
                f"{dest.name} already exists in the app folder.\n"
                "Overwrite with the selected file?")
            if not overwrite:
                return dest  # use existing
        try:
            shutil.copy2(src, dest)
            self._log(f"Copied {src.name} into app folder.\n", "info")
        except Exception as exc:
            messagebox.showerror("Copy failed", str(exc))
            return None
        return dest

    # ── Auto-detect *.a26 / *.bin / *.a26inp already present ─────────

    def _auto_detect(self):
        roms = (sorted(APP_DIR.glob("*.a26")) +
                sorted(APP_DIR.glob("*.A26")) +
                sorted(APP_DIR.glob("*.bin")) +
                sorted(APP_DIR.glob("*.BIN")))
        inputs = (sorted(APP_DIR.glob("*.a26inp")) +
                  sorted(APP_DIR.glob("*.A26INP")))
        # deduplicate (case-insensitive Windows may return same file)
        seen_rom, seen_inp = set(), set()
        rom_unique, inp_unique = [], []
        for p in roms:
            low = p.name.lower()
            if low not in seen_rom:
                seen_rom.add(low)
                rom_unique.append(p)
        for p in inputs:
            low = p.name.lower()
            if low not in seen_inp:
                seen_inp.add(low)
                inp_unique.append(p)

        if len(rom_unique) == 1:
            self.rom_var.set(rom_unique[0].name)
        if len(inp_unique) == 1:
            self.movie_var.set(inp_unique[0].name)

    # ── Checks ───────────────────────────────────────────────────────

    def _check_executables(self):
        missing = []
        if not RECORDFUN.exists():
            missing.append("recordfun.exe")
        if not LEARNFUN.exists():
            missing.append("learnfun.exe")
        if not PLAYFUN.exists():
            missing.append("playfun.exe")
        if not REPLAYFUN.exists():
            missing.append("replayfun.exe")
        if missing:
            self._log(
                f"WARNING: {', '.join(missing)} not found in app folder!\n",
                "err")

    def _watch_replay(self):
        """Launch replayfun.exe to visually replay an .a26inp file."""
        rom = self.rom_var.get().strip()
        if not rom:
            messagebox.showwarning("Missing ROM", "Please select a ROM file.")
            return
        if not (APP_DIR / rom).exists():
            messagebox.showerror("ROM not found",
                                 f"{rom} not found in app folder.")
            return
        if not REPLAYFUN.exists():
            messagebox.showerror("Not found", "replayfun.exe not found.")
            return

        # Figure out which .a26inp to replay.
        game = Path(rom).stem
        movie = self.movie_var.get().strip()

        # Prefer playfun's progress output if it exists.
        progress = f"{game}-playfun-futures-progress.a26inp"
        if (APP_DIR / progress).exists():
            replay_file = progress
        elif movie and (APP_DIR / movie).exists():
            replay_file = movie
        else:
            messagebox.showwarning(
                "No input log",
                "No .a26inp file found to replay.\n"
                "Record a game or run playfun first.")
            return

        # Write config for replayfun.
        CONFIG.write_text(
            f"game {game}\nrom {rom}\nmovie {replay_file}\n",
            encoding="utf-8")

        cmd = [str(REPLAYFUN), replay_file]
        self._log(f"Replaying: {replay_file}\n", "info")
        self._log(f"Controls: Space=pause, Right=step, "
                  "Up/Down=speed, R=restart, ESC=quit\n\n", "info")
        try:
            subprocess.Popen(cmd, cwd=str(APP_DIR))
        except Exception as exc:
            messagebox.showerror("Replay launch failed", str(exc))

    def _validate_inputs(self) -> bool:
        rom = self.rom_var.get().strip()
        movie = self.movie_var.get().strip()
        if not rom:
            messagebox.showwarning("Missing ROM", "Please select a ROM file.")
            return False
        if not movie:
            messagebox.showwarning("Missing Input Log",
                                   "Please select an input log file.")
            return False
        if not (APP_DIR / rom).exists():
            messagebox.showerror("ROM not found",
                                 f"{rom} not found in app folder.")
            return False
        if not (APP_DIR / movie).exists():
            messagebox.showerror("Input log not found",
                                 f"{movie} not found in app folder.")
            return False
        return True

    # ── Config file ──────────────────────────────────────────────────

    def _write_config(self):
        rom = self.rom_var.get().strip()
        movie = self.movie_var.get().strip()
        game = Path(rom).stem
        CONFIG.write_text(
            f"game {game}\nrom {rom}\nmovie {movie}\n",
            encoding="utf-8")
        self._log(f"Wrote config.txt  (game={game}, rom={rom}, movie={movie})\n",
                  "info")

    # ── Process execution ────────────────────────────────────────────

    def _set_running(self, running: bool):
        state = "disabled" if running else "normal"
        self.btn_record.configure(state=state)
        self.btn_learn.configure(state=state)
        self.btn_play.configure(state=state)
        # Replay is always enabled — it launches a separate window and
        # is most useful while playfun is running to check progress.
        self.btn_replay.configure(state="normal")
        self.btn_stop.configure(state="normal" if running else "disabled")

    def _run_recordfun(self):
        """Launch recordfun.exe to let the user play and record inputs."""
        rom = self.rom_var.get().strip()
        if not rom:
            messagebox.showwarning("Missing ROM", "Please select a ROM file.")
            return
        if not (APP_DIR / rom).exists():
            messagebox.showerror("ROM not found",
                                 f"{rom} not found in app folder.")
            return
        if not RECORDFUN.exists():
            messagebox.showerror("Not found", "recordfun.exe not found.")
            return

        # Write config so recordfun can find the ROM.
        game = Path(rom).stem
        movie = self.movie_var.get().strip() or f"{game}.a26inp"
        CONFIG.write_text(
            f"game {game}\nrom {rom}\nmovie {movie}\n",
            encoding="utf-8")

        # Check for existing input log.
        outfile = APP_DIR / f"{game}.a26inp"
        if outfile.exists():
            ans = messagebox.askyesno(
                "Input log exists",
                f"{outfile.name} already exists.\n"
                "Recording will overwrite it.\n\n"
                "Continue?")
            if not ans:
                return

        self._log("Launch recordfun — play the game in the SDL window.\n"
                  "Press ESC or close the window when done.\n"
                  "Controls: Arrow keys=Move, Z/Space=Fire, "
                  "Enter=Reset, Tab=Select\n\n", "info")

        # Run recordfun in its own console so it gets the SDL window.
        # We don't capture its stdout — it runs independently.
        try:
            proc = subprocess.Popen(
                [str(RECORDFUN)],
                cwd=str(APP_DIR))
            self._log(f"recordfun.exe started (PID {proc.pid}).\n"
                      "Waiting for it to finish…\n", "info")
            # Wait in a background thread so the GUI stays responsive.
            def _wait():
                rc = proc.wait()
                tag = "info" if rc == 0 else "err"
                self.root.after(0, self._log,
                    f"\nrecordfun exited with code {rc}\n", tag)
                if rc == 0:
                    # Auto-set the movie var to the new recording.
                    self.root.after(0, self.movie_var.set, f"{game}.a26inp")
                    self.root.after(0, self._log,
                        f"Input log saved: {game}.a26inp\n"
                        "You can now run Pretrain (learnfun).\n", "info")
            threading.Thread(target=_wait, daemon=True).start()
        except Exception as exc:
            messagebox.showerror("Launch failed", str(exc))

    def _run_learnfun(self):
        if not self._validate_inputs():
            return
        if not LEARNFUN.exists():
            messagebox.showerror("Not found", "learnfun.exe not found.")
            return
        self._write_config()
        self._exec([str(LEARNFUN)], "Learnfun (Pretrain)")

    def _get_num_helpers(self) -> int:
        """Resolve helper count: 'auto' → cpu_count-1, otherwise the int."""
        h = self.helpers_var.get().strip().lower()
        if h == "auto" or not h:
            n = (os.cpu_count() or 2) - 1
            return max(n, 1)
        try:
            return max(int(h), 1)
        except ValueError:
            return max((os.cpu_count() or 2) - 1, 1)

    def _update_port_label(self):
        """Show the effective port range in the UI."""
        try:
            start = self.port_var.get()
        except tk.TclError:
            start = 8000
        n = self._get_num_helpers()
        end = start + n - 1
        self.port_label.configure(
            text=f"(ports {start}\u2013{end}  \u2192  {n} helper(s))")

    def _run_playfun(self):
        if not self._validate_inputs():
            return
        if not PLAYFUN.exists():
            messagebox.showerror("Not found", "playfun.exe not found.")
            return
        self._write_config()

        # Check for existing progress file on fresh (non-resume) runs.
        if not self.resume_var.get():
            game = Path(self.rom_var.get().strip()).stem
            progress = APP_DIR / f"{game}-playfun-futures-progress.a26inp"
            if progress.exists():
                ans = messagebox.askyesno(
                    "Existing progress found",
                    f"A progress file already exists:\n{progress.name}\n\n"
                    "A fresh run will overwrite it.\n"
                    "Use the Resume checkbox to continue instead.\n\n"
                    "Start fresh anyway?")
                if not ans:
                    return

        num_helpers = self._get_num_helpers()
        try:
            start_port = self.port_var.get()
        except tk.TclError:
            start_port = 8000

        ports = [start_port + i for i in range(num_helpers)]
        self._exec_playfun(ports)

    # ── Single-process execution (learnfun) ──────────────────────────

    def _exec(self, cmd: list[str], label: str):
        self._clear_log()
        self._log(f"─── Starting {label} ───\n", "info")
        self._log(f"$ {' '.join(cmd)}\n\n")
        self.status_var.set(f"Running {label}…")
        self.stop_event.clear()
        self._set_running(True)
        t = threading.Thread(target=self._reader_thread,
                             args=(cmd, label), daemon=True)
        t.start()

    def _reader_thread(self, cmd: list[str], label: str):
        try:
            self.process = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                cwd=str(APP_DIR),
                creationflags=subprocess.CREATE_NO_WINDOW)

            for raw_line in iter(self.process.stdout.readline, b""):
                if self.stop_event.is_set():
                    break
                line = raw_line.decode("utf-8", errors="replace")
                self.root.after(0, self._log, line)

            self.process.stdout.close()
            rc = self.process.wait()
            tag = "info" if rc == 0 else "err"
            msg = f"\n─── {label} exited with code {rc} ───\n"
            self.root.after(0, self._log, msg, tag)
        except Exception as exc:
            self.root.after(0, self._log, f"\nERROR: {exc}\n", "err")
        finally:
            self.process = None
            self.root.after(0, self._set_running, False)
            self.root.after(0, self.status_var.set, "Ready.")

    # ── Multi-process execution (playfun helpers + master) ───────────

    def _exec_playfun(self, ports: list[int]):
        self._clear_log()
        self.stop_event.clear()
        self._set_running(True)

        self._log(f"─── Starting Playfun ───\n", "info")
        self._log(f"Helpers: {len(ports)}   Ports: "
                  f"{ports[0]}\u2013{ports[-1]}\n\n", "info")
        self.status_var.set(f"Running Playfun ({len(ports)} helpers)…")

        t = threading.Thread(target=self._playfun_thread,
                             args=(ports,), daemon=True)
        t.start()

    def _playfun_thread(self, ports: list[int]):
        exe = str(PLAYFUN)
        cwd = str(APP_DIR)

        CREATE_NEW_CONSOLE = 0x00000010
        IDLE_PRIORITY_CLASS = 0x00000040

        # 1. Spawn helper processes (each gets its own console window)
        for port in ports:
            if self.stop_event.is_set():
                break
            cmd = [exe, "--helper", str(port)]
            self.root.after(0, self._log, f"  Spawning helper on port {port}\n")
            try:
                p = subprocess.Popen(
                    cmd,
                    cwd=cwd,
                    creationflags=CREATE_NEW_CONSOLE | IDLE_PRIORITY_CLASS)
                self.helper_procs.append(p)
            except Exception as exc:
                self.root.after(0, self._log,
                                f"Failed to start helper on port {port}: "
                                f"{exc}\n", "err")

        if self.stop_event.is_set():
            self._cleanup_helpers()
            return

        # 2. Wait for helpers to bind their ports
        self.root.after(0, self._log,
                        "Waiting for helpers to initialize…\n", "info")
        for _ in range(30):
            if self.stop_event.is_set():
                self._cleanup_helpers()
                return
            time.sleep(0.1)

        # Check that helpers are still alive
        dead = [i for i, p in enumerate(self.helper_procs)
                if p.poll() is not None]
        if dead:
            self.root.after(0, self._log,
                            f"WARNING: {len(dead)} helper(s) exited during "
                            f"startup!\n", "err")

        # Bring the GUI back to the foreground
        self.root.after(0, self._raise_window)

        # 3. Start master
        port_args = [str(p) for p in ports]
        cmd = [exe, "--master"] + port_args
        if self.resume_var.get():
            cmd.append("--resume")
        self.root.after(0, self._log,
                        f"\n$ {' '.join(cmd)}\n\n")

        try:
            self.process = subprocess.Popen(
                cmd,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                cwd=cwd,
                creationflags=subprocess.CREATE_NO_WINDOW)

            for raw_line in iter(self.process.stdout.readline, b""):
                if self.stop_event.is_set():
                    break
                line = raw_line.decode("utf-8", errors="replace")
                self.root.after(0, self._log, line)

            self.process.stdout.close()
            rc = self.process.wait()
            tag = "info" if rc == 0 else "err"
            msg = f"\n─── Master exited with code {rc} ───\n"
            self.root.after(0, self._log, msg, tag)
        except Exception as exc:
            self.root.after(0, self._log, f"\nERROR: {exc}\n", "err")
        finally:
            self.process = None
            self._cleanup_helpers()
            self.root.after(0, self._set_running, False)
            self.root.after(0, self.status_var.set, "Ready.")

    def _cleanup_helpers(self):
        """Terminate all helper processes."""
        for p in self.helper_procs:
            try:
                p.terminate()
            except OSError:
                pass
        for p in self.helper_procs:
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                try:
                    p.kill()
                except OSError:
                    pass
        self.helper_procs.clear()

    def _stop_process(self):
        self.stop_event.set()
        self._log("\nStopping all processes…\n", "err")
        if self.process:
            try:
                self.process.terminate()
            except OSError:
                pass
        self._cleanup_helpers()

    # ── Log widget helpers ───────────────────────────────────────────

    def _log(self, text: str, tag: str | None = None):
        text = _ANSI_RE.sub('', text)
        self.log.configure(state="normal")
        if tag:
            self.log.insert("end", text, tag)
        else:
            self.log.insert("end", text)
        self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _raise_window(self):
        """Bring the GUI window to the foreground."""
        self.root.lift()
        self.root.attributes('-topmost', True)
        self.root.after(100, lambda: self.root.attributes('-topmost', False))
        self.root.focus_force()


def main():
    root = tk.Tk()
    try:
        from ctypes import windll
        windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        pass
    TASBotGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
