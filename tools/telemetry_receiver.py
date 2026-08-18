#!/usr/bin/env python3
"""Minimal local receiver for validating ESP32 IoT Bridge HTTP telemetry."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json


class TelemetryHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        try:
            telemetry = json.loads(body)
        except json.JSONDecodeError:
            self.send_error(400, "Expected JSON")
            return

        print(json.dumps(telemetry, ensure_ascii=False), flush=True)
        self.send_response(204)
        self.end_headers()

    def log_message(self, format, *args):
        return


if __name__ == "__main__":
    server = ThreadingHTTPServer(("0.0.0.0", 8080), TelemetryHandler)
    print("Listening for ESP32 telemetry on http://0.0.0.0:8080/telemetry", flush=True)
    server.serve_forever()
