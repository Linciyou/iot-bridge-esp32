# ESP32-S3 IoT Bridge

ESP-IDF firmware for forwarding telemetry from an ESP32-S3 to an HTTP service over Wi-Fi. The project uses FreeRTOS tasks and a queue so sensor collection does not block on network I/O.

## Architecture

`telemetry_source` produces a sample every two seconds. `cloud_bridge` receives it from a FreeRTOS queue and sends a JSON HTTP POST after Wi-Fi is connected.

```
telemetry source -> FreeRTOS queue -> Wi-Fi station -> HTTP POST receiver
```

The sample currently generates temperature and light values. Replace `telemetry_producer_task()` with an ADC, I2C, UART, or GPIO data source without changing the transport task.

## Build and flash

Use an ASCII-only path on Windows, for example `C:\esp\iot-bridge-esp32`; ESP-IDF configuration can stall when the project path contains non-ASCII characters.

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

In `menuconfig`, open **IoT bridge configuration** and set the Wi-Fi SSID, password, and HTTP endpoint. Do not commit the generated `sdkconfig` file because it may contain credentials.

The default endpoint is suitable for a local receiver:

```text
http://192.168.1.100:8080/telemetry
```

## Local end-to-end test

Run the receiver on the computer that is connected to the same 2.4 GHz network:

```powershell
python tools/telemetry_receiver.py
```

Set the endpoint to the computer's LAN address, for example `http://192.168.0.101:8080/telemetry`. A successful transfer is visible in both places:

```text
I (...) iot_bridge: Forwarded sequence=11, HTTP 204
```

```json
{"device_id":"E8F60A8AC9A4","sequence":11,"uptime_ms":22194,"temperature_c":25.2,"light_raw":1012}
```

## Behavior on failure

- Wi-Fi reconnects up to eight times after a disconnect.
- If Wi-Fi is unavailable for 15 seconds, the item being handled is dropped and the next item is processed.
- If the queue fills, the newest sample is dropped and a warning is logged.
- The sample receiver is HTTP-only. Use HTTPS with certificate verification before sending data outside a trusted network.

## Verified hardware path

Tested on an ESP32-S3 (QFN56, 8 MB PSRAM) using the native USB Serial/JTAG port. The validated path is FreeRTOS queue -> Wi-Fi -> HTTP POST -> local receiver.
