# hrm-fan-control

Replace regular AC fan speed switch (eg: low, mid, high) with a relay switch controlled by an ESP32 board, based on HR zones of a BLE HR Monitor.

This version can "remember" the "last paired" BLE HRM device and only reconnects to it (instead of connecting to the first available BLE HRM). It also implements a HTTP server where the "last paired" BLE HRM device can be "cleared" along with the WiFi settings and HR zones changed. An bypass switch allows for the Fan work as before (speed controlled by fan switch). Default WiFi configuration is Access Point mode but this can be change to Access Point station via the HTTP server.

Parts List:
- ESP32 Dev Board (use partition scheme "Minimal SPIFFS" on Arduino editor)
- 4-way Relay Switch
- Wire Jumpers
- Breadboard (or solder wire jumpers)
- Blue and Green LEDs:
  - Blue is for Bluetooth
    - Short on, short off means searching for a HRM
    - Long on with short off means connected to HRM
      - one blink for Zone 1
      - two blinks for Zone 2
      - three blinks for Zone 3
  - Green is for WiFi
    - Long on, short off means WiFi in AP/Hotspot mode
    - Short on, long off means WiFi in station mode and connected to AP
    - Short on, short off means s WiFi in station mode and trying to connect to AP
- 330 Ohm 1/4W resistor (one per LED)

----------------------------------------
Based on:
- [GitHub agrabbs/hrm_fan_control](https://github.com/agrabbs/hrm_fan_control) by [Andrew Grabbs](https://github.com/agrabbs)
- [ESP32 Async Web Server – Control Outputs with Arduino IDE](https://randomnerdtutorials.com/esp32-async-web-server-espasyncwebserver-library/) by [Random Nerd Tutorials](https://randomnerdtutorials.com/)
- [ESP32 Flash Memory – Store Permanent Data (Write and Read)](https://randomnerdtutorials.com/esp32-flash-memory/) by [Random Nerd Tutorials](https://randomnerdtutorials.com/)
