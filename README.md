# ESP32-S3 IoT Bridge

Small ESP-IDF example for sending samples from an ESP32-S3 to an HTTP endpoint. It has two FreeRTOS tasks: one produces samples and one uploads them. A queue sits between them so a slow network request does not hold up the data source.

The demo source generates temperature and light values. In a real project, replace the body of `telemetry_producer_task()` with the ADC, I2C, UART, or GPIO read that you need.

## Build

ESP-IDF on Windows is happier when the project is in an ASCII-only path. `C:\esp\iot-bridge-esp32` is a safe choice.

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

Open **IoT bridge configuration** in `menuconfig` and enter the Wi-Fi SSID, password, and receiver URL. `sdkconfig` is ignored on purpose: it can contain credentials.

The code was tested with an ESP32-S3 (QFN56, 8 MB PSRAM) over its native USB Serial/JTAG port.

## Quick test

Start the included receiver on a computer connected to the same 2.4 GHz network:

```powershell
python tools/telemetry_receiver.py
```

Set the endpoint to that computer's LAN address, for example `http://192.168.0.101:8080/telemetry`. The receiver prints each JSON payload and the ESP32 log shows a `HTTP 204` response:

```text
I (...) iot_bridge: sent sequence=11, HTTP 204
```

For the test board, this path was exercised end to end: sample task -> queue -> Wi-Fi -> HTTP POST -> local receiver.

## Notes

- The station reconnects up to eight times after a disconnect.
- A queue full warning means the source is producing faster than data can be sent.
- The included receiver is for LAN testing. Use HTTPS and server certificate verification for anything outside a trusted network.
