from __future__ import annotations

import argparse
import itertools
import socket
from typing import Any

import requests
from flask import Flask, jsonify, request


def create_app(esp32_base_url: str, device_number: int = 0, esp32_hostname: str = "asi585mc-cooler", fallback_url: str = "") -> Flask:
    app = Flask(__name__)
    tx_counter = itertools.count(1)
    state = {"connected": False}
    session = requests.Session()

    def normalized_base_urls() -> list[str]:
        urls: list[str] = []
        if esp32_base_url:
            urls.append(esp32_base_url.rstrip("/"))
        if esp32_hostname:
            urls.append(f"http://{esp32_hostname}.local")
            try:
                resolved_ip = socket.gethostbyname(f"{esp32_hostname}.local")
                urls.append(f"http://{resolved_ip}")
            except OSError:
                pass
        if fallback_url:
            urls.append(fallback_url.rstrip("/"))

        unique_urls: list[str] = []
        for url in urls:
            if url and url not in unique_urls:
                unique_urls.append(url)
        return unique_urls

    def request_esp32(method: str, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
        last_error: Exception | None = None
        for base_url in normalized_base_urls():
            try:
                response = session.request(method, f"{base_url}{path}", json=payload, timeout=5)
                response.raise_for_status()
                return response.json()
            except requests.RequestException as error:
                last_error = error
        if last_error is None:
            raise requests.RequestException("No ESP32 endpoint configured")
        raise last_error

    def next_tx() -> int:
        return next(tx_counter)

    def client_tx_id() -> int:
        value = request.values.get("ClientTransactionID", request.args.get("ClientTransactionID", 0))
        try:
            return int(value)
        except (TypeError, ValueError):
            return 0

    def alpaca_response(value: Any = None, error_number: int = 0, error_message: str = ""):
        payload = {
            "ClientTransactionID": client_tx_id(),
            "ServerTransactionID": next_tx(),
            "ErrorNumber": error_number,
            "ErrorMessage": error_message,
        }
        if value is not None:
            payload["Value"] = value
        return jsonify(payload)

    def esp_get(path: str) -> dict[str, Any]:
        return request_esp32("GET", path)

    def esp_post(path: str, payload: dict[str, Any]) -> dict[str, Any]:
        return request_esp32("POST", path, payload)

    def current_status() -> dict[str, Any]:
        return esp_get("/api/astro/status")

    def require_connection() -> bool:
        return state["connected"]

    def bool_param(name: str) -> bool | None:
        raw = request.values.get(name)
        if raw is None and request.is_json:
            raw = (request.get_json(silent=True) or {}).get(name)
        if raw is None:
            return None
        if isinstance(raw, bool):
            return raw
        return str(raw).strip().lower() in {"1", "true", "on", "yes"}

    def float_param(name: str) -> float | None:
        raw = request.values.get(name)
        if raw is None and request.is_json:
            raw = (request.get_json(silent=True) or {}).get(name)
        if raw is None:
            return None
        try:
            return float(raw)
        except (TypeError, ValueError):
            return None

    @app.get("/management/apiversions")
    def management_versions():
        return alpaca_response([1])

    @app.get("/management/v1/description")
    def management_description():
        return alpaca_response(
            {
                "ServerName": "ASI585MC Cooler Alpaca Bridge",
                "Manufacturer": "OpenCode",
                "ManufacturerVersion": "0.1.0",
                "Location": "Local PC bridge",
            }
        )

    @app.get("/management/v1/configureddevices")
    def configured_devices():
        return alpaca_response(
            [
                {
                    "DeviceName": "ASI585MC Cooler Bridge",
                    "DeviceType": "Camera",
                    "DeviceNumber": device_number,
                    "UniqueID": "asi585mc-cooler-bridge-camera-0",
                }
            ]
        )

    @app.route(f"/api/v1/camera/{device_number}/connected", methods=["GET", "PUT"])
    def connected():
        if request.method == "PUT":
            connected_value = bool_param("Connected")
            if connected_value is None:
                return alpaca_response(error_number=1025, error_message="Connected parameter missing")
            state["connected"] = connected_value
            return alpaca_response()
        return alpaca_response(state["connected"])

    @app.get(f"/api/v1/camera/{device_number}/name")
    def name():
        return alpaca_response("ASI585MC Cooler Bridge")

    @app.get(f"/api/v1/camera/{device_number}/description")
    def description():
        return alpaca_response("Bridge Alpaca Camera focalizzato sul controllo del cooler ESP32")

    @app.get(f"/api/v1/camera/{device_number}/driverinfo")
    def driver_info():
        return alpaca_response("Bridge HTTP verso controller ESP32 WiFi/BLE per cella Peltier")

    @app.get(f"/api/v1/camera/{device_number}/driverversion")
    def driver_version():
        return alpaca_response("0.1.0")

    @app.get(f"/api/v1/camera/{device_number}/interfaceversion")
    def interface_version():
        return alpaca_response(3)

    @app.get(f"/api/v1/camera/{device_number}/supportedactions")
    def supported_actions():
        return alpaca_response(["dewpoint", "ambienttemperature", "controllerstatus"])

    @app.put(f"/api/v1/camera/{device_number}/action")
    def action():
        if not require_connection():
            return alpaca_response(error_number=1031, error_message="Camera not connected")
        action_name = request.values.get("Action", "").strip().lower()
        status = current_status()
        if action_name == "dewpoint":
            return alpaca_response(str(status.get("dew_point_c")))
        if action_name == "ambienttemperature":
            return alpaca_response(str(status.get("ambient_c")))
        if action_name == "controllerstatus":
            return alpaca_response(str(status))
        return alpaca_response(error_number=1026, error_message="Action not supported")

    @app.get(f"/api/v1/camera/{device_number}/cangetcoolerpower")
    def can_get_cooler_power():
        return alpaca_response(True)

    @app.get(f"/api/v1/camera/{device_number}/cansetccdtemperature")
    def can_set_ccd_temperature():
        return alpaca_response(True)

    @app.get(f"/api/v1/camera/{device_number}/cooleron")
    def cooler_on_get():
        if not require_connection():
            return alpaca_response(error_number=1031, error_message="Camera not connected")
        return alpaca_response(bool(current_status().get("cooler_on", False)))

    @app.put(f"/api/v1/camera/{device_number}/cooleron")
    def cooler_on_put():
        if not require_connection():
            return alpaca_response(error_number=1031, error_message="Camera not connected")
        cooler_on = bool_param("CoolerOn")
        if cooler_on is None:
            return alpaca_response(error_number=1025, error_message="CoolerOn parameter missing")
        esp_post("/api/astro/cooler", {"enabled": cooler_on})
        return alpaca_response()

    @app.get(f"/api/v1/camera/{device_number}/setccdtemperature")
    def set_ccd_temperature_get():
        if not require_connection():
            return alpaca_response(error_number=1031, error_message="Camera not connected")
        return alpaca_response(float(current_status().get("setpoint_c", 0.0)))

    @app.put(f"/api/v1/camera/{device_number}/setccdtemperature")
    def set_ccd_temperature_put():
        if not require_connection():
            return alpaca_response(error_number=1031, error_message="Camera not connected")
        target = float_param("SetCCDTemperature")
        if target is None:
            return alpaca_response(error_number=1025, error_message="SetCCDTemperature parameter missing")
        esp_post("/api/astro/setpoint", {"target_c": target})
        return alpaca_response()

    @app.get(f"/api/v1/camera/{device_number}/ccdtemperature")
    def ccd_temperature():
        if not require_connection():
            return alpaca_response(error_number=1031, error_message="Camera not connected")
        return alpaca_response(float(current_status().get("temperature_c", 0.0)))

    @app.get(f"/api/v1/camera/{device_number}/heatsinktemperature")
    def heatsink_temperature():
        if not require_connection():
            return alpaca_response(error_number=1031, error_message="Camera not connected")
        return alpaca_response(float(current_status().get("heat_sink_c", 0.0)))

    @app.get(f"/api/v1/camera/{device_number}/coolerpower")
    def cooler_power():
        if not require_connection():
            return alpaca_response(error_number=1031, error_message="Camera not connected")
        return alpaca_response(float(current_status().get("cooler_power_pct", 0.0)))

    @app.get(f"/api/v1/camera/{device_number}/sensorname")
    def sensor_name():
        return alpaca_response("ASI585MC")

    @app.get(f"/api/v1/camera/{device_number}/sensortype")
    def sensor_type():
        return alpaca_response(1)

    @app.get(f"/api/v1/camera/{device_number}/maxbinx")
    def max_binx():
        return alpaca_response(1)

    @app.get(f"/api/v1/camera/{device_number}/maxbiny")
    def max_biny():
        return alpaca_response(1)

    @app.errorhandler(requests.RequestException)
    def handle_http_error(error: requests.RequestException):
        return alpaca_response(error_number=1280, error_message=f"ESP32 bridge error: {error}")

    return app


def main() -> None:
    parser = argparse.ArgumentParser(description="ASCOM Alpaca camera cooler bridge for ESP32 cooler controller")
    parser.add_argument("--esp32-url", default="http://asi585mc-cooler.local", help="Base URL of the ESP32 controller")
    parser.add_argument("--esp32-hostname", default="asi585mc-cooler", help="mDNS hostname without .local")
    parser.add_argument("--esp32-fallback-url", default="", help="Optional fallback URL like http://192.168.1.50")
    parser.add_argument("--host", default="0.0.0.0", help="Host address for the bridge")
    parser.add_argument("--port", default=11111, type=int, help="Port for the Alpaca bridge")
    args = parser.parse_args()

    app = create_app(args.esp32_url.rstrip("/"), esp32_hostname=args.esp32_hostname, fallback_url=args.esp32_fallback_url)
    app.run(host=args.host, port=args.port)


if __name__ == "__main__":
    main()
