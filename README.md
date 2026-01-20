# ESP32 Wi-Fi Performance Measurement
## Overview
This project aims to measure and compare the Wi-Fi performance of different ESP32 boards, with a focus on real application-level performance.

The measured parameters include:
- RSSI (Received Signal Strength Indicator) [dBm]
- TCP (Transmission Control Protocol) Throughput [Mbps]

Data is transmitted from the ESP32 via Serial in JSON format, then recorded, analyzed, and visualized on a PC.

## System Architecture
The ESP32 is responsible for:
- Connecting to a Wi-Fi network
- Measuring RSSI from the Wi-Fi driver
- Sending TCP data to the PC to calculate throughput
- Publishing all measurement data via Serial

The PC (Python) is responsible for:
- Receiving data from the Serial port
- Logging data to CSV files
- Performing statistical analysis
- Displaying real-time graphs

## Visualization
The following graphs are used for analysis:
- RSSI vs Sample
- Throughput vs Sample

## Requirements
- ESP-IDF v5.5.1
- Python 3.10+
- pyserial
- matplotlib
- pandas

## Measurement Method
### RSSI
RSSI values are obtained using esp_wifi_sta_get_ap_info().
The unit is dBm (negative values, where values closer to 0 indicate stronger signal strength).

### Throughput
Throughput is calculated based on the number of bytes successfully transmitted through a TCP socket per second.

## Data Format
```json
{
  "ts": 158.127632,
  "rssi": -55,
  "throughput": 3.21
}
```
The data is logged and saved in CSV format as follows:
| esp_ts_sec | rssi (dBm) | throughput_mbps (Mbps) |
|------------|------------|------------------------|
| 158.127632 | -55        | 3.21                   |

## Statistical Analysis
The collected data is grouped into windows of 30 samples, and the following statistical metrics are calculated:
- Mean
- Median
- Minimum
- Maximum
- Standard deviation

These statistics are used to compare Wi-Fi performance across different MCU boards.

## How to Run
1. Prepare the ESP-IDF Environment
    - Open the ESP project folder.
    - Open an ESP-IDF terminal (VS Code: F1 → ESP-IDF: Open ESP-IDF Terminal).
    - Connect the ESP32 board to your computer via USB.
2. Configure Console Output
    - Set the console output to USB Serial/JTAG Controller.
      ``` bash
      idf.py menuconfig
      ```
    - Navigate to
      ``` bash
      Component config
       └─ ESP System Settings
         └─ Channel for console output
             └─ USB Serial/JTAG Controller
      ```
    - Press s to save
    - Press q to quit
3. Build and Flash Firmware
    ``` bash
    idf.py build flash
    ```
4. Start iPerf Server on PC
    - Open another terminal (e.g. PowerShell) and run:
      ``` bash
      iperf -s
      ```
    > **Note:** iPerf version **2.x** is required.
5. Configure Python Logger (main/wifi_test.py)
    - Edit the following parameters in:
      ```python
      DEFAULT_PORT = 'COM19'        # Windows: COMx, Linux/Mac: /dev/ttyUSBx
      DEFAULT_BAUD = 115200
      DEFAULT_PC_IP = "192.168.10.22" # Your PC's IP address
      CSV_PATH = os.path.join(BASE_DIR, "mgi_stats_samples.csv") # rename the csv file
      ```
6. Collect Data
    ```bash
    python main/wifi_test.py
    ```
    - Collect at least 30 samples
    - Stop the program with Ctrl + C when finished
7. Run Statistical Analysis
    - Edit (main/analysis.py):
      ```python
      CSV_PATH = os.path.join(BASE_DIR, "mgi_stats_samples.csv") # rename the csv file
      stats.to_csv(os.path.join(BASE_DIR, "mgi_stats_30samples.csv")) # rename the csv file
      ```
    - Run
      ```bash
      python main/analysis.py
      ```

## Output Files
| File | Description | 
|------------|------------|
| *_stats_samples.csv | Raw measurement data | 
| *_stats_30samples.csv | Statistical results (30-sample windows) | 

## Notes / Limitations
The measured throughput represents application-level TCP throughput, which may differ from the theoretical Wi-Fi PHY data rate.
Results are influenced by environmental factors such as interference, distance to the access point, and antenna orientation.

## References
1. Espressif Systems: [**ESP-IDF Wi-Fi iPerf Example (v5.5.2)**](https://github.com/espressif/esp-idf/tree/v5.5.2/examples/wifi/iperf)

