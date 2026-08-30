"""Generate scenario-based telemetry and send it to the analysis API."""

import argparse
import json
import random
import sys
import time
from copy import deepcopy
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

BASE_TELEMETRY: dict[str, Any] = {
    "device_id": "esp32-node-01",

    "timestamp_ms": 0,
    "dht11": {"valid": True, "temperature_c": 29.0, "humidity_percent": 55.0},
    "lm35": {"valid": True, "temperature_c": 29.2},
    "hc_sr04": {
        "valid": True,
        "echo_time_us": 4100,
        "distance_cm": 70.0,
        "water_height_cm": 30.0,
    },
    "steam_sensor": {"valid": True, "adc_raw": 600, "wet_percent": 20.0},
    "water_sensor": {"valid": True, "adc_raw": 750, "level_percent": 25.0},
    "ks0272_vibration": {
        "valid": True,
        "current_raw": 1500,
        "min_raw": 1460,
        "max_raw": 1540,
        "peak_to_peak_raw": 80,
        "mean_raw": 1500.0,
        "rms_raw": 18.0,
        "event_count": 0,
        "saturated": False,
    },
    "all_sensors_valid": True,
}


def generate_telemetry(scenario: str, rng: random.Random) -> dict[str, Any]:
    data = deepcopy(BASE_TELEMETRY)
    data["timestamp_ms"] = int(time.time() * 1000)

    if scenario == "rain":
        data["dht11"].update(humidity_percent=rng.uniform(88, 96))
        data["steam_sensor"].update(
            adc_raw=rng.randint(2350, 2800), wet_percent=rng.uniform(78, 94)
        )
        data["water_sensor"].update(
            adc_raw=rng.randint(1200, 1800), level_percent=rng.uniform(40, 60)
        )
    elif scenario == "flood":
        height = rng.uniform(85, 96)
        data["dht11"].update(humidity_percent=rng.uniform(90, 99))
        data["steam_sensor"].update(
            adc_raw=rng.randint(2600, 3000), wet_percent=rng.uniform(88, 100)
        )
        data["water_sensor"].update(adc_raw=rng.randint(2700, 3000), level_percent=height)
        data["hc_sr04"].update(distance_cm=100 - height, water_height_cm=height, echo_time_us=700)
    elif scenario == "vibration":
        peak_to_peak = rng.randint(650, 900)
        data["ks0272_vibration"].update(
            current_raw=1800,
            min_raw=1200,
            max_raw=1200 + peak_to_peak,
            peak_to_peak_raw=peak_to_peak,
            mean_raw=1550.0,
            rms_raw=rng.uniform(115, 180),
            event_count=rng.randint(8, 20),
        )
    elif scenario == "earthquake":
        data["ks0272_vibration"].update(
            current_raw=2300,
            min_raw=500,
            max_raw=3300,
            peak_to_peak_raw=2800,
            mean_raw=1550.0,
            rms_raw=480.0,
            event_count=42,
        )
    elif scenario == "blizzard":
        data["dht11"].update(temperature_c=-12.0, humidity_percent=92.0)
        data["lm35"].update(temperature_c=-11.5)
        data["steam_sensor"].update(adc_raw=1800, wet_percent=60.0)
        data["water_sensor"].update(adc_raw=300, level_percent=10.0)
    elif scenario == "compound":
        data = generate_telemetry("flood", rng)
        vibration = generate_telemetry("vibration", rng)["ks0272_vibration"]
        data["ks0272_vibration"] = vibration
    elif scenario == "sensor_error":
        data["dht11"].update(valid=False, temperature_c=None, humidity_percent=None)
        data["all_sensors_valid"] = False
    elif scenario != "normal":
        raise ValueError(f"Unknown scenario: {scenario}")

    return data


def generate_sensor_data(scenario: str, intensity: float = 1.0) -> dict[str, Any]:
    rng = random.Random()
    base_data = generate_telemetry(scenario.lower(), rng)
    return base_data

def list_scenarios() -> list[str]:
    return ["NORMAL", "FLOOD", "EARTHQUAKE", "SNOWSTORM"]

def post_telemetry(url: str, device_key: str, telemetry: dict[str, Any]) -> dict[str, Any]:
    request = Request(
        url,
        data=json.dumps(telemetry).encode("utf-8"),
        headers={"Content-Type": "application/json", "X-Device-Key": device_key},
        method="POST",
    )
    try:
        with urlopen(request, timeout=30) as response:  # noqa: S310 - URL is supplied by user.
            return json.load(response)
    except HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Server returned HTTP {exc.code}: {body}") from exc
    except URLError as exc:
        raise RuntimeError(f"Cannot connect to server: {exc.reason}") from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--scenario",
        choices=[
            "normal",
            "rain",
            "flood",
            "vibration",
            "earthquake",
            "blizzard",
            "compound",
            "sensor_error",
        ],
        default="normal",
    )
    parser.add_argument("--url", default="http://127.0.0.1:8000/api/v1/analyze")
    parser.add_argument("--device-id", default="esp32-node-01")
    parser.add_argument("--device-key", default="test-device-key")
    parser.add_argument("--count", type=int, default=1)

    parser.add_argument("--interval", type=float, default=2.0)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--print-only", action="store_true")
    return parser.parse_args()


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    args = parse_args()
    if args.count < 1 or args.interval < 0:
        raise SystemExit("--count must be >= 1 and --interval must be >= 0")
    rng = random.Random(args.seed)
    for index in range(args.count):
        telemetry = generate_telemetry(args.scenario, rng)
        print(f"\n[{index + 1}/{args.count}] telemetry ({args.scenario})")
        print(json.dumps(telemetry, ensure_ascii=False, indent=2))
        if not args.print_only:
            result = post_telemetry(args.url, args.device_key, telemetry)
            outputs = result["outputs"]
            print(
                f"result: {result['risk_level']} / {result['hazard']} "
                f"({result['confidence_percent']}%)"
            )
            print(f"advice: {result['advice']}")
            print(f"outputs: LED={outputs['led_color']}, buzzer={outputs['buzzer_mode']}")
        if index + 1 < args.count:
            time.sleep(args.interval)


if __name__ == "__main__":
    main()
