""" ESP32 Wi-Fi Tester 
You can use this script to command an ESP32 device over serial to perform Wi-Fi tests.
It will start an iPerf test to a specified server IP and parse RSSI and throughput data
from the serial output, displaying them in real-time graphs.

All data is also logged to a CSV file for further analysis.

More Info: https://github.com/Nuos-Magicko/ESP-WiFi-Tester/tree/mai-wifi-dev
"""

from collections import deque
import csv
import json
import time
import threading
import argparse
import os
import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

# --- USER CONFIGURATION ---
# Default settings (can be overridden by command line args)
DEFAULT_PORT = 'COM6'        # Windows: COMx, Linux/Mac: /dev/ttyUSBx
DEFAULT_BAUD = 115200
DEFAULT_PC_IP = "192.168.10.22" # Your PC's IP address
# --------------------------

TIME_WINDOW = 30  # seconds

# Global State
rssi_buffer = deque(maxlen=500)        # (time, rssi)
throughput_buffer = deque(maxlen=500)  # (time, mbps)
stop_event = threading.Event()

SAMPLE_COUNT = 30

rssi_samples = []
tp_samples = []
ts_samples = []

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CSV_PATH = os.path.join(BASE_DIR, "mgi_stats_samples.csv")

def write_raw_csv(ts: float, rssi:int, tp:float):
    """Write raw CSV data to a file.

    Args:
        ts (float): ESP timestamp in seconds
        rssi (int): RSSI value in dBm
        tp (float): Throughput in Mbps
    """
    file_exists = os.path.exists(CSV_PATH)

    with open(CSV_PATH, "a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)

        if not file_exists:
            writer.writerow(["esp_ts_sec","rssi","throughput_mbps"])

        writer.writerow([round(ts, 6),rssi,round(tp, 2)])

def parse_arguments() -> argparse.Namespace:
    """Parse command line arguments.

    Returns:
        argparse.Namespace: Parsed arguments
    """
    parser = argparse.ArgumentParser(description='ESP32 Wi-Fi Test Commander')
    parser.add_argument('--port', type=str, default=DEFAULT_PORT, help='Serial Port')
    parser.add_argument('--ip', type=str, default=DEFAULT_PC_IP, help='PC IP Address (Server)')
    return parser.parse_args()

def serial_handler(ser: serial.Serial, pc_ip: str):
    """Handle serial communication with the ESP32.

    Args:
        ser (serial.Serial): Serial connection object
        pc_ip (str): IP address of the PC (iPerf server)
    """

    print(f"Opening Serial on {ser.port}...")

    # Wait for boot
    time.sleep(2)

    # Send a few 'enters' to clear the console prompt
    ser.write(b'\n\n')
    time.sleep(0.5)

    print(f"Sending iPerf Command targeting {pc_ip}...")

    while not stop_event.is_set():
        try:
            if ser.in_waiting:
                # Read binary and decode, ignoring weird startup characters
                line = ser.readline().decode('utf-8', errors='replace').strip()
                print(f"RAW: {line}")  # Debug: Print raw line

                if not line:
                    continue

                # --- PARSER 1: RSSI DATA ---
                # Look for our custom tag: DATA:{"rssi": -50, "ch": 6}
                if line.startswith("DATA:"):
                    try:
                        json_str = line.replace("DATA:", "")
                        data = json.loads(json_str)

                        if "rssi" in data and "throughput" in data and "ts" in data:
                            ts_esp = data["ts"] / 1_000_000
                            rssi = data["rssi"]
                            tp   = data["throughput"]

                            now = time.time()
                            rssi_buffer.append((now, rssi))
                            throughput_buffer.append((now, tp))

                            write_raw_csv(ts_esp, rssi, tp)

                    except json.JSONDecodeError:
                        pass

                # --- PARSER 2: SYSTEM LOGS ---
                else:
                    # Print normal logs slightly dimmed or prefixed
                    print(f"[LOG]: {line}")

        except serial.SerialException as e:
            print(f"Serial Error: {e}")
            break

def main() -> None:
    """Main entry point of the ESP32 Wi-Fi monitoring application.

    Returns:
        None: This function does not return any value.
    """
    args = parse_arguments()

    try:
        ser = serial.Serial(args.port, DEFAULT_BAUD, timeout=0.1)
    except serial.SerialException as e:
        print(f"Could not open port {args.port}: {e}")
        return

    # Start Serial Thread
    t = threading.Thread(target=serial_handler, args=(ser, args.ip))
    t.daemon = True
    t.start()

    # Setup Graph
    fig, ax = plt.subplots(figsize=(10, 6))

    # Style the plot
    ax.set_title(f"ESP32 Wi-Fi Signal Strength (Target: {args.ip})")
    ax.set_ylabel("RSSI (dBm)")
    ax.set_xlabel("Time (Samples)")
    ax.set_ylim(-100, -10)
    ax.grid(True, linestyle='--', alpha=0.7)

    # RSSI Line
    line_rssi, = ax.plot([], [], color='#00ff00', linewidth=2, label='RSSI')

    # Background color for dark mode feel (Optional)
    ax.set_facecolor('#1e1e1e')
    fig.patch.set_facecolor('#121212')
    ax.tick_params(colors='white')
    ax.yaxis.label.set_color('white')
    ax.xaxis.label.set_color('white')
    ax.title.set_color('white')

    ax2 = ax.twinx()
    ax2.set_ylabel("Throughput (Mbps)")
    ax2.set_ylim(0, 10)
    ax2.tick_params(colors='cyan')
    ax2.yaxis.label.set_color('cyan')

    line_tp, = ax2.plot([], [], color='cyan', linewidth=2, label='Throughput')

    lines = [line_rssi, line_tp]
    labels = [l.get_label() for l in lines]
    ax.legend(lines, labels, loc='upper right')

    # Animation Update
    def update(_frame: int):
        """Update RSSI and throughput plots for each animation frame.
        
        This function is called periodically by Matplotlib's FuncAnimation
        to refresh the real-time Wi-Fi measurement graphs.

        Args:
            _frame (int): The current animation frame number.

        Returns:
            tuple: A tuple containing the updated plot lines.
        """
        now = time.time()

        # ----- RSSI -----
        rssi_data = [(t, v) for t, v in rssi_buffer if now - t <= TIME_WINDOW]
        if rssi_data:
            x_rssi = [t - now for t, _ in rssi_data]   # time relative (negative)
            y_rssi = [v for _, v in rssi_data]
            line_rssi.set_data(x_rssi, y_rssi)

        # ----- Throughput -----
        tp_data = [(t, v) for t, v in throughput_buffer if now - t <= TIME_WINDOW]
        if tp_data:
            x_tp = [t - now for t, _ in tp_data]
            y_tp = [v for _, v in tp_data]
            line_tp.set_data(x_tp, y_tp)

        ax.set_xlim(-TIME_WINDOW, 0)
        ax2.set_xlim(-TIME_WINDOW, 0)

        return line_rssi, line_tp

    _ani = FuncAnimation(fig, update, interval=100, blit=False)

    print("Starting GUI... Press Ctrl+C in console to stop.")
    plt.show()

    stop_event.set()
    ser.close()

if __name__ == "__main__":
    main()
