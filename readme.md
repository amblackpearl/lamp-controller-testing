
# ESP32 Lamp Controller Testing Module

This module provides comprehensive testing and monitoring functionality for an ESP32-based lamp controller system. It measures and validates six critical parameters to classify if the lamp controller is functioning properly or not.

## Technologies & Framework

### Firmware
- **Platform**: Arduino IDE with ESP32 core
- **Microcontroller**: ESP32 (Wi-Fi enabled)
- **Programming Language**: C++ (Arduino)

### Libraries & Frameworks
- **LittleFS**: File system for storing web assets
- **WiFi.h**: Built-in library for Wi-Fi connectivity
- **WebSockets.h**: For real-time bidirectional communication
- **HardwareSerial.h**: For RS485 serial communication
- **U8g2lib.h**: For controlling the 128x64 LCD display

### Communication Protocols
- **Wi-Fi**: For wireless connectivity and web server hosting
- **WebSocket**: For real-time data streaming to web clients
- **RS485**: For wired serial communication
- **HTTP**: For serving web interface

### Frontend
- **HTML5**: Semantic markup for the web-based monitoring interface
- **CSS3**: Custom animations and styling with imported Google Fonts (Inter family)
- **Tailwind CSS**: Utility-first CSS framework loaded via CDN for rapid UI development
- **Vanilla JavaScript**: For client-side logic, DOM manipulation, and WebSocket communication
- **WebSocket API**: For real-time bidirectional communication with the ESP32
- **SVG**: For vector graphics (Logo-SEI.svg)
- **Responsive Design**: Mobile-first approach with CSS Grid and Tailwind responsive utilities
- **Google Fonts**: Inter font family for modern typography

### Development Tools
- **Arduino IDE**: For firmware development and uploading
- **LittleFS Plugin**: For uploading file system data
- **Git**: For version control

## Features

- **Automated Testing**: Performs automated testing of lamp hardware components
- **Real-time Monitoring**: Monitors electrical performance in real-time via multiple interfaces
- **Quality Classification**: Automatically detects failures or degradation for quality assessment
- **Multi-interface Support**: Offers monitoring through LCD, wired RS485, and wireless WebSocket connections

## Test Results

- ✅ **Good Condition (Passed)**

  ![Good](/assets/good.png)

- ❌ **Not Good Condition (Failed)**

  ![Not Good](/assets/ng.png)

## Tested Parameters

- **Voltage**: Monitors the input or operating voltage of the lamp circuit to ensure it's within acceptable range
- **Current**: Measures the current draw of the lamp to detect faults or anomalies that could indicate component failure
- **Power**: Calculates the total power consumption (Voltage × Current) to verify energy efficiency and component health
- **IR (Infrared)**: Tests IR sensor or remote control reception capability to ensure proper remote functionality
- **Built-in Blue LED**: It's an indicator if the battery is above 80%
- **Built-in Red LED**: It's an indicator if the battery is below 20%

## Hardware Components

- **Microcontroller**: ESP32 for processing and connectivity
- **Display**: 128x64 LCD for real-time monitoring on a panel box
- **Communication**: RS485 interface for wired monitoring
- **Connectivity**: WiFi for wireless monitoring via WebSocket

## Monitoring Interfaces

- **Local Display**: Real-time monitoring on a 128x64 LCD panel
- **Wired Interface**: RS485 connection to a custom [RS485 Python application](https://github.com/amblackpearl/RS485-SerialApp)
- **Wireless Interface**: WebSocket-enabled web server for real-time data streaming and remote monitoring

## Project Structure

```
lamp-controller-testing/
├── Lamp-Controller-Tesing.ino    # Main Arduino firmware
├── readme.md                     # This documentation
├── assets/                       # Images for documentation
│   ├── good.png
│   └── ng.png
└── data/                         # Web server files
    ├── index.html
    └── Logo-SEI.svg
```

## Setup Instructions

1. Install the ESP32 board package in Arduino IDE
2. Install the LittleFS plugin from [earlephilhower/arduino-littlefs-upload](https://github.com/earlephilhower/ arduino-littlefs-upload)
3. Upload the LittleFS filesystem using the plugin by pressing `Ctrl+Shift+p` then choose ESP32 LittleFS Data Upload
4. Upload the `Lamp-Controller-Tesing.ino` firmware to your ESP32 board
5. Connect the required sensors and components as per the hardware schematic
6. Configure the WiFi credentials in the firmware
7. Access the web interface via the IP address displayed on the LCD
8. Monitor the test results through the web interface or connected RS485 application

## Panel Box Status Indicators

- **Green LED**: Indicates that system is start the operation
- **Red LED**: Indicates that system is not operating
- **LCD Display**: Shows real-time measurements and system status
  
