
# ESP32 Lamp Controller Testing Module

This module provides comprehensive testing and monitoring functionality for an ESP32-based lamp controller system. It measures and validates six critical parameters to clasify if the lamp controller is good or not good.

## Main Purposes:
  - Perform automated testing of lamp hardware components
  - Monitor electrical performance in real-time via multiple interfaces
  - Detect failures or degradation automatically for a classification
  - Validate LED color indicators are functioning correctly
  - Ensure IR receiver operation for remote control compatibility
  - Provide diagnostic feedback for quality classification

## Tested Parameters:
  - Voltage: Monitors the input or operating voltage of the lamp circuit
  - Current: Measures the current draw of the lamp to detect faults or anomalies
  - Power: Calculates the total power consumption (Voltage × Current)
  - IR (Infrared): Tests IR sensor or remote control reception capability
  - Built-in Blue LED: Validates the functionality and operation of the lamp's blue LED
  - Built-in Red LED: Validates the functionality and operation of the lamp's red LED
  
## Monitoring Capabilities:
  - Real-Time monitoring on a panel box through lcd 128x64
  - Wired monitoring via RS485 interface connected to a custom [RS485 Python application](https://github.com/amblackpearl/RS485-SerialApp)
  - Wireless monitoring through a WebSocket-enabled web server for real-time data streaming
  
