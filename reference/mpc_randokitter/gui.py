"""customtkinter GUI for MPC RandoKitter."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
import traceback
from pathlib import Path
from tkinter import filedialog, messagebox

import customtkinter as ctk

from mpc_randokitter import core, __version__


SETTINGS_PATH = Path.home() / ".mpc_randokitter.json"


def _load_settings() -> dict:
    try:
        with open(SETTINGS_PATH, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def _save_settings(data: dict) -> None:
    try:
        with open(SETTINGS_PATH, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
    except OSError:
        pass


def _reveal(path: str) -> None:
    """Open a folder in the OS file browser."""
    if sys.platform == "darwin":
        subprocess.Popen(["open", path])
    elif sys.platform.startswith("win"):
        subprocess.Popen(["explorer", path])
    else:
        subprocess.Popen(["xdg-open", path])


class KitterApp(ctk.CTk):
    def __init__(self) -> None:
        super().__init__()
        ctk.set_appearance_mode("System")
        ctk.set_default_color_theme("blue")

        self.title(f"MPC Random Kit Maker v{__version__}")
        self.geometry("520x460")
        self.resizable(False, False)

        settings = _load_settings()
        self._worker: threading.Thread | None = None
        self._cancel = threading.Event()
        self._last_output: str | None = None

        self._build_ui(settings)

    # ---------- UI ----------

    def _build_ui(self, settings: dict) -> None:
        pad = {"padx": 16, "pady": 6}

        ctk.CTkLabel(self, text="Samples folder", anchor="w").pack(fill="x", **pad)
        row = ctk.CTkFrame(self, fg_color="transparent")
        row.pack(fill="x", padx=16)
        self.source_var = ctk.StringVar(value=settings.get("source_dir", ""))
        ctk.CTkEntry(row, textvariable=self.source_var, state="readonly").pack(
            side="left", fill="x", expand=True, padx=(0, 6)
        )
        ctk.CTkButton(row, text="Browse…", width=90,
                      command=self._pick_source).pack(side="left")

        ctk.CTkLabel(self, text="Output folder", anchor="w").pack(fill="x", **pad)
        row = ctk.CTkFrame(self, fg_color="transparent")
        row.pack(fill="x", padx=16)
        self.output_var = ctk.StringVar(
            value=settings.get("output_dir", str(Path.home() / "Desktop"))
        )
        ctk.CTkEntry(row, textvariable=self.output_var, state="readonly").pack(
            side="left", fill="x", expand=True, padx=(0, 6)
        )
        ctk.CTkButton(row, text="Browse…", width=90,
                      command=self._pick_output).pack(side="left")

        opts = ctk.CTkFrame(self, fg_color="transparent")
        opts.pack(fill="x", padx=16, pady=(12, 0))
        ctk.CTkLabel(opts, text="Number of kits:").grid(row=0, column=0, sticky="w")
        self.kits_var = ctk.StringVar(value=str(settings.get("num_kits", 4)))
        ctk.CTkOptionMenu(opts, variable=self.kits_var,
                         values=[str(n) for n in range(1, 11)],
                         width=80).grid(row=0, column=1, padx=(8, 20))
        ctk.CTkLabel(opts, text="Pads per kit:").grid(row=0, column=2, sticky="w")
        self.pads_var = ctk.StringVar(value=str(settings.get("pads", 64)))
        ctk.CTkOptionMenu(opts, variable=self.pads_var,
                         values=["16", "32", "64", "128"],
                         width=80).grid(row=0, column=3, padx=(8, 0))

        ctk.CTkLabel(self, text="Kit name (optional)", anchor="w").pack(fill="x", **pad)
        self.name_var = ctk.StringVar(value="")
        ctk.CTkEntry(self, textvariable=self.name_var,
                     placeholder_text="leave blank for random names").pack(
            fill="x", padx=16
        )

        self.progress = ctk.CTkProgressBar(self)
        self.progress.set(0.0)
        self.progress.pack(fill="x", padx=16, pady=(18, 4))

        self.status_var = ctk.StringVar(value="Ready.")
        self.status_label = ctk.CTkLabel(self, textvariable=self.status_var,
                                         anchor="w")
        self.status_label.pack(fill="x", padx=16)

        self.reveal_btn = ctk.CTkButton(self, text="Reveal output",
                                        width=140, command=self._reveal_output)
        self.reveal_btn.pack(pady=(8, 0))
        self.reveal_btn.pack_forget()  # hidden until a job completes

        buttons = ctk.CTkFrame(self, fg_color="transparent")
        buttons.pack(pady=16)
        self.generate_btn = ctk.CTkButton(buttons, text="Generate",
                                          width=140, command=self._on_generate)
        self.generate_btn.grid(row=0, column=0, padx=8)
        ctk.CTkButton(buttons, text="Quit", width=100,
                      command=self._on_quit).grid(row=0, column=1, padx=8)

    # ---------- Actions ----------

    def _pick_source(self) -> None:
        path = filedialog.askdirectory(title="Pick samples folder",
                                       initialdir=self.source_var.get() or str(Path.home()))
        if path:
            self.source_var.set(path)

    def _pick_output(self) -> None:
        path = filedialog.askdirectory(title="Pick output folder",
                                       initialdir=self.output_var.get() or str(Path.home()))
        if path:
            self.output_var.set(path)

    def _reveal_output(self) -> None:
        if self._last_output and os.path.isdir(self._last_output):
            _reveal(self._last_output)

    def _on_quit(self) -> None:
        if self._worker and self._worker.is_alive():
            if not messagebox.askyesno("Quit", "A job is running. Cancel and quit?"):
                return
            self._cancel.set()
        self._persist_settings()
        self.destroy()

    def _persist_settings(self) -> None:
        _save_settings({
            "source_dir": self.source_var.get(),
            "output_dir": self.output_var.get(),
            "num_kits": int(self.kits_var.get()),
            "pads": int(self.pads_var.get()),
        })

    def _on_generate(self) -> None:
        if self._worker and self._worker.is_alive():
            # In-flight: treat as cancel
            self._cancel.set()
            self.generate_btn.configure(state="disabled", text="Cancelling…")
            return

        source = self.source_var.get().strip()
        output = self.output_var.get().strip()
        if not source or not os.path.isdir(source):
            messagebox.showerror("Missing input", "Pick a valid samples folder.")
            return
        if not output:
            messagebox.showerror("Missing input", "Pick an output folder.")
            return

        num_kits = int(self.kits_var.get())
        pads = int(self.pads_var.get())
        name = self.name_var.get().strip() or None

        self._persist_settings()
        self._cancel.clear()
        self.reveal_btn.pack_forget()
        self.progress.set(0.0)
        self.status_var.set("Starting…")
        self.generate_btn.configure(text="Cancel")

        self._worker = threading.Thread(
            target=self._run_job,
            args=(source, output, num_kits, pads, name),
            daemon=True,
        )
        self._worker.start()

    # ---------- Worker thread ----------

    def _run_job(self, source: str, output: str, num_kits: int,
                 pads: int, name: str | None) -> None:
        try:
            core.generate_kits(
                source_dir=source,
                output_dir=output,
                num_kits=num_kits,
                pads_per_kit=pads,
                name_override=name,
                progress_cb=self._progress_cb,
                cancel_event=self._cancel,
            )
            cancelled = self._cancel.is_set()
            self.after(0, lambda: self._on_done(output, cancelled, None))
        except Exception as exc:
            tb = traceback.format_exc()
            self.after(0, lambda: self._on_done(output, False, (exc, tb)))

    def _progress_cb(self, fraction: float, status: str) -> None:
        # Marshal onto the main thread — tkinter isn't thread-safe.
        self.after(0, lambda: (self.progress.set(fraction),
                               self.status_var.set(status)))

    def _on_done(self, output: str, cancelled: bool,
                 error: tuple[Exception, str] | None) -> None:
        self.generate_btn.configure(state="normal", text="Generate")
        if error:
            exc, tb = error
            self.status_var.set(f"Error: {exc}")
            self.progress.set(0.0)
            messagebox.showerror("Kit generation failed", f"{exc}\n\n{tb}")
            return
        if cancelled:
            self.status_var.set("Cancelled.")
            self.progress.set(0.0)
            return
        self._last_output = output
        self.reveal_btn.pack(pady=(8, 0))


def main() -> None:
    app = KitterApp()
    app.mainloop()


if __name__ == "__main__":
    main()
