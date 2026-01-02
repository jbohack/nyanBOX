#!/usr/bin/env python3
#
# nyanBOX by Nyan Devices
# https://github.com/jbohack/nyanBOX
# Copyright (c) 2025 jbohack
#
# Licensed under the MIT License
# https://opensource.org/licenses/MIT
#
# SPDX-License-Identifier: MIT
#

"""
nyanBOX Display Mirror Viewer
Cross-platform OLED framebuffer viewer with auto-connection and mirroring.
Works on Windows, macOS, and Linux out of the box.

Usage:
    python display_viewer.py [port]

Example:
    python display_viewer.py              # Auto-detect device
    python display_viewer.py /dev/ttyUSB0 # Specific port
"""

import sys
import time
import threading
import platform
from queue import Queue, Empty

try:
    import tkinter as tk
except ImportError:
    print("Error: tkinter is not installed")
    print("Install: sudo apt-get install python3-tk (Linux)")
    print("         brew install python-tk (macOS)")
    sys.exit(1)

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: pyserial is not installed")
    print("Install: pip install pyserial")
    sys.exit(1)


class DisplayViewer:
    WIDTH = 128
    HEIGHT = 64
    SCALE = 4
    GAP = 6
    STATUS_BAR_HEIGHT = 12

    BAUD_RATE = 115200
    RECONNECT_DELAY = 2.0
    READ_DELAY = 0.01

    DISPLAY_UPDATE_MS = 33
    STATUS_UPDATE_MS = 500

    def __init__(self, port=None):
        """
        Args:
            port: Serial port path (optional, will auto-detect if None)
        """
        self.port = port
        self.serial = None
        self.running = True
        self.connected = False
        self.mirror_enabled = False

        self.latest_frame = None
        self.frame_queue = Queue(maxsize=10)

        self._setup_ui()

        self._start_threads()

        self._update_display()
        self._update_status()

        self.root.mainloop()

    def _setup_ui(self):
        self.root = tk.Tk()
        self.root.title("nyanBOX Display Mirror")
        self.root.configure(bg="#111111")
        self.root.resizable(False, False)

        canvas_width = self.WIDTH * self.SCALE
        canvas_height = self.HEIGHT * self.SCALE + self.GAP + self.STATUS_BAR_HEIGHT

        self.canvas = tk.Canvas(
            self.root,
            width=canvas_width,
            height=canvas_height,
            bg="#111111",
            highlightthickness=0,
            bd=0
        )
        self.canvas.pack()

        self.pixels = []
        for y in range(self.HEIGHT):
            row = []
            for x in range(self.WIDTH):
                rect = self.canvas.create_rectangle(
                    x * self.SCALE,
                    y * self.SCALE,
                    (x + 1) * self.SCALE,
                    (y + 1) * self.SCALE,
                    outline="",
                    fill="#222222"
                )
                row.append(rect)
            self.pixels.append(row)

        status_y = self.HEIGHT * self.SCALE + self.GAP
        self.status_rect = self.canvas.create_rectangle(
            0, status_y,
            canvas_width, status_y + self.STATUS_BAR_HEIGHT,
            fill="#AA0000",
            outline=""
        )

        self.status_text = self.canvas.create_text(
            canvas_width // 2,
            status_y + self.STATUS_BAR_HEIGHT // 2,
            text="Initializing...",
            fill="white",
            font=("Helvetica", 10, "bold")
        )

        self.root.protocol("WM_DELETE_WINDOW", self._on_exit)

    def _start_threads(self):
        threading.Thread(target=self._serial_manager, daemon=True).start()
        threading.Thread(target=self._serial_reader, daemon=True).start()

    def _find_device(self):
        """Auto-detect nyanBOX serial port across platforms.

        Returns:
            str: Serial port path or None if not found
        """
        ports = serial.tools.list_ports.comports()

        chip_keywords = [
            "cp210", "ch340", "ftdi", "usb serial", "uart",
            "slab", "silicon labs", "wch", "prolific"
        ]

        system = platform.system()
        if system == "Windows":
            port_patterns = ["com"]
        elif system == "Darwin":
            port_patterns = ["tty.usb", "tty.wch", "tty.slab", "cu.usb", "cu.wch", "cu.slab"]
        else:
            port_patterns = ["ttyusb", "ttyacm", "ttyama"]

        for port in ports:
            desc = (port.description or "").lower()
            name = (port.device or "").lower()

            if any(keyword in desc for keyword in chip_keywords):
                return port.device

            if any(pattern in name for pattern in port_patterns):
                return port.device

        return None

    def _serial_manager(self):
        while self.running:
            if not self.serial or not self.serial.is_open:
                self.connected = False
                self.mirror_enabled = False

                try:
                    target_port = self.port or self._find_device()

                    if target_port:
                        self.serial = serial.Serial(
                            target_port,
                            self.BAUD_RATE,
                            timeout=1
                        )
                        time.sleep(0.5)

                        self.serial.reset_input_buffer()
                        self.serial.reset_output_buffer()

                        self.serial.write(b"MIRROR_ON\n")
                        self.serial.flush()

                        self.connected = True
                        self.mirror_enabled = True
                        print(f"Connected to {target_port} @ {self.BAUD_RATE} baud")
                    else:
                        time.sleep(self.RECONNECT_DELAY)

                except serial.SerialException as e:
                    print(f"Serial error: {e}")
                    self._close_serial()
                    time.sleep(self.RECONNECT_DELAY)

                except Exception as e:
                    print(f"Unexpected error: {e}")
                    self._close_serial()
                    time.sleep(self.RECONNECT_DELAY)
            else:
                time.sleep(1)

    def _serial_reader(self):
        buffer = bytearray()

        while self.running:
            if not self.serial or not self.serial.is_open:
                time.sleep(0.5)
                continue

            try:
                if self.serial.in_waiting:
                    data = self.serial.read(self.serial.in_waiting)
                    buffer.extend(data)

                    while True:
                        start = buffer.find(b"<FB>")
                        end = buffer.find(b"</FB>")

                        if start == -1 or end == -1 or end <= start:
                            break

                        frame_data = buffer[start + 4:end]
                        buffer = buffer[end + 5:]

                        if len(frame_data) >= 2:
                            size = frame_data[0] | (frame_data[1] << 8)
                            framebuffer = frame_data[2:2 + size]

                            if len(framebuffer) == size:
                                try:
                                    self.frame_queue.put_nowait(framebuffer)
                                except:
                                    try:
                                        self.frame_queue.get_nowait()
                                        self.frame_queue.put_nowait(framebuffer)
                                    except:
                                        pass

                    if len(buffer) > 4096:
                        buffer = buffer[-2048:]
                else:
                    time.sleep(self.READ_DELAY)

            except serial.SerialException as e:
                print(f"Serial disconnected: {e}")
                self._close_serial()
                time.sleep(self.RECONNECT_DELAY)

            except Exception as e:
                print(f"Read error: {e}")
                self._close_serial()
                time.sleep(self.RECONNECT_DELAY)

    def _update_display(self):
        try:
            while True:
                self.latest_frame = self.frame_queue.get_nowait()
        except Empty:
            pass

        if self.latest_frame:
            fb = self.latest_frame

            for page in range(self.HEIGHT // 8):
                for x in range(self.WIDTH):
                    idx = page * self.WIDTH + x

                    if idx >= len(fb):
                        continue

                    byte_val = fb[idx]

                    for bit in range(8):
                        y = page * 8 + bit

                        if y >= self.HEIGHT:
                            continue

                        pixel = (byte_val >> bit) & 1
                        color = "#FFFFFF" if pixel else "#222222"
                        self.canvas.itemconfig(self.pixels[y][x], fill=color)

        if self.running:
            self.root.after(self.DISPLAY_UPDATE_MS, self._update_display)

    def _update_status(self):
        if self.connected:
            self.canvas.itemconfig(self.status_rect, fill="#00AA00")
            self.canvas.itemconfig(self.status_text, text="Connected")
        else:
            self.canvas.itemconfig(self.status_rect, fill="#AA0000")
            port_info = f" ({self.port})" if self.port else ""
            self.canvas.itemconfig(self.status_text, text=f"Searching{port_info}")

        if self.running:
            self.root.after(self.STATUS_UPDATE_MS, self._update_status)

    def _close_serial(self):
        if self.serial:
            try:
                if self.serial.is_open:
                    self.serial.close()
            except:
                pass
            finally:
                self.serial = None
                self.connected = False
                self.mirror_enabled = False

    def _on_exit(self):
        self.running = False

        if self.serial and self.serial.is_open:
            try:
                self.serial.write(b"MIRROR_OFF\n")
                self.serial.flush()
                time.sleep(0.1)
            except:
                pass

        self._close_serial()
        self.root.destroy()
        sys.exit(0)


def main():
    print("nyanBOX Display Mirror Viewer")
    print(f"Platform: {platform.system()} {platform.release()}")
    print(f"Python: {sys.version.split()[0]}")
    print()

    port = None
    if len(sys.argv) > 1:
        port = sys.argv[1]
        print(f"Using specified port: {port}")
    else:
        print("Auto-detecting device...")

    print()

    try:
        DisplayViewer(port)
    except KeyboardInterrupt:
        print("\nExiting...")
        sys.exit(0)
    except Exception as e:
        print(f"Fatal error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()