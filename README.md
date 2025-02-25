# Deepwoods Device Detection

Deepwoods Device Detection is a security and monitoring project built around the [XIAO ESP32 C3](https://www.arduino.cc/en/Main/ArduinoBoardXIAO-ESP32C3) and the [Heltec LoRa V3](https://heltec.org/project/wifi-lora-32/). The device is designed to perform an initial baseline scan of nearby Bluetooth (BT/BLE) and WiFi devices for 7 minutes upon boot, whitelisting every device detected. After the baseline period, it continuously scans for new devices and sends an alert over UART to Meshtastic if any non-whitelisted device is detected.

## Overview

- **Initial Scan:** Upon powering on, the device scans for Bluetooth (BT/BLE) and WiFi devices for a total of 7 minutes.
- **Whitelisting:** Every device detected during the initial scan is automatically whitelisted.
- **Continuous Monitoring:** After the baseline scan, the board continues scanning for both Bluetooth and WiFi.
- **Alert System:** If a device that was not captured in the baseline scan (i.e., a non-whitelisted device) is detected during continuous monitoring, an alert message is sent via UART to a connected Meshtastic device.

## Features

- **Automated Baseline Detection:** Simplifies device management by automatically whitelisting known devices.
- **Real-Time Monitoring:** Keeps an ongoing check on the environment for any unauthorized device appearances.
- **Alert Integration:** Seamlessly integrates with Meshtastic through UART, allowing for prompt notifications.
- **Dual Wireless Support:** Utilizes both Bluetooth (BT/BLE) and WiFi scanning for comprehensive coverage.

## Hardware Requirements

- **XIAO ESP32 C3:** Serves as the main controller and wireless scanning unit.
- **Heltec LoRa V3:** Provides LoRa connectivity and integrates with the device for Meshtastic alerts.
- **Additional Components:** Mesh Detect board [](https://www.tindie.com/products/colonel_panic/mesh-detect-2/)
## Software

- **Firmware:** The firmware is written in C/C++  for the ESP32 environment.
- **UART Communication:** Configured for sending alert messages to the Meshtastic device.
- **Wireless Libraries:** Utilizes libraries for Bluetooth, BLE, and WiFi scanning.

## Setup & Installation


2. **Firmware Installation:**
   - Clone this repository.
   - Open the project in your favorite IDE (e.g., Arduino IDE or PlatformIO).
   - Compile and flash the firmware onto the XIAO ESP32 C3.
   - Verify that the device successfully boots and begins the initial 7-minute scan.

3. **Configuration:**
   - Modify any configuration parameters (e.g., scan duration, UART settings) in the configuration files if necessary.
   - Customize the whitelist behavior as needed.

## How It Works

- **Boot Sequence:** On power-up, the device begins a 7-minute scan across Bluetooth and WiFi frequencies.
- **Whitelisting Process:** Every device detected during this period is stored in a whitelist.
- **Continuous Monitoring:** Once the baseline period ends, the device continues to scan both Bluetooth and WiFi channels.
- **Alert Trigger:** Detection of any device not present in the initial whitelist triggers an alert message sent over UART to the connected Meshtastic device.
- **Meshtastic Integration:** The alert mechanism allows for real-time notifications, leveraging the Meshtastic network for broader message dissemination.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request with your enhancements or bug fixes. For major changes, please open an issue first to discuss your ideas.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for more details.

---

*Happy hacking!*
