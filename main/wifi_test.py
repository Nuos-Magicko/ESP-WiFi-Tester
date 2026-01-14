import sys
import serial
import json
import time
import threading
import argparse
import re
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque

# --- USER CONFIGURATION ---
# Default settings (can be overridden by command line args)
DEFAULT_PORT = 'COM3'        # Windows: COMx, Linux/Mac: /dev/ttyUSBx
DEFAULT_BAUD = 115200
DEFAULT_PC_IP = "192.168.1.100" # Your PC's IP address
# --------------------------

# Global State
rssi_buffer = deque([0]*200, maxlen=200)
throughput_buffer = deque([0]*200, maxlen=200) # Placeholder if we parse speed later
stop_threads = False
esp_connected = False

def parse_arguments():
    parser = argparse.ArgumentParser(description='ESP32 Wi-Fi Test Commander')
    parser.add_argument('--port', type=str, default=DEFAULT_PORT, help='Serial Port')
    parser.add_argument('--ip', type=str, default=DEFAULT_PC_IP, help='PC IP Address (Server)')
    return parser.parse_args()

def serial_handler(ser, pc_ip):
    global esp_connected
    
    print(f"Opening Serial on {ser.port}...")
    
    # Wait for boot
    time.sleep(2) 
    
    # Send a few 'enters' to clear the console prompt
    ser.write(b'\n\n') 
    time.sleep(0.5)

    # 1. Start the iPerf test automatically after a short delay
    # We send the command: iperf -c <IP> -i 1 -t 6000
    # -i 1: Report every second
    # -t 6000: Run for 100 minutes (effectively forever for testing)
    print(f"Sending iPerf Command targeting {pc_ip}...")
    cmd = f"iperf -c {pc_ip} -i 1 -t 6000\n"
    ser.write(cmd.encode())

    while not stop_threads:
        try:
            if ser.in_waiting:
                # Read binary and decode, ignoring weird startup characters
                line = ser.readline().decode('utf-8', errors='replace').strip()
                
                if not line:
                    continue

                # --- PARSER 1: RSSI DATA ---
                # Look for our custom tag: DATA:{"rssi": -50, "ch": 6}
                if line.startswith("DATA:"):
                    try:
                        json_str = line.replace("DATA:", "")
                        data = json.loads(json_str)
                        rssi_buffer.append(data.get("rssi", -100))
                    except ValueError:
                        pass
                
                # --- PARSER 2: IPERF SPEED ---
                # Standard iPerf output looks like: 
                # [ 0] 3.0- 4.0 sec  1.25 MBytes  10.5 Mbits/sec
                elif "Mbits/sec" in line:
                    print(f"SPEED: {line}") # Print speed to console
                    # Optional: Regex to extract speed number for a second graph
                    # match = re.search(r'([\d\.]+)\s+Mbits/sec', line)
                
                # --- PARSER 3: SYSTEM LOGS ---
                else:
                    # Print normal logs slightly dimmed or prefixed
                    print(f"[LOG]: {line}")

        except Exception as e:
            print(f"Serial Error: {e}")
            break

def main():
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
    ax.legend(loc='upper right')
    
    # Background color for dark mode feel (Optional)
    ax.set_facecolor('#1e1e1e')
    fig.patch.set_facecolor('#121212')
    ax.tick_params(colors='white')
    ax.yaxis.label.set_color('white')
    ax.xaxis.label.set_color('white')
    ax.title.set_color('white')
    
    # Animation Update
    def update(frame):
        line_rssi.set_data(range(len(rssi_buffer)), rssi_buffer)
        return line_rssi,

    ani = FuncAnimation(fig, update, interval=100, blit=True)
    
    print("Starting GUI... Press Ctrl+C in console to stop.")
    plt.show()

    global stop_threads
    stop_threads = True
    ser.close()

if __name__ == "__main__":
    main()
