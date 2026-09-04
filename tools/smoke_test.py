#!/usr/bin/env python3
import argparse
import json
import socket
import sys
import time


def send(host: str, port: int, payload: dict) -> dict:
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n")
        line = sock.makefile("rb").readline()
    if not line:
        raise RuntimeError("server closed connection without a response")
    return json.loads(line.decode("utf-8"))


def expect_success(name: str, response: dict) -> dict:
    if not response.get("success"):
        raise AssertionError(f"{name} failed: {response}")
    print(f"[OK] {name}")
    return response["data"]


def expect_failure(name: str, response: dict) -> None:
    if response.get("success"):
        raise AssertionError(f"{name} unexpectedly succeeded: {response}")
    print(f"[OK] {name}: {response.get('error')}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Phase 0/1 protocol checks against a running server.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=45454)
    args = parser.parse_args()

    expect_success("ping", send(args.host, args.port, {"action": "ping"}))

    user_login = expect_success(
        "user.login",
        send(args.host, args.port, {"action": "user.login", "phone": "13800000001"}),
    )
    user_token = user_login["session"]["token"]

    expect_failure(
        "frozen user.login",
        send(args.host, args.port, {"action": "user.login", "phone": "13800000002"}),
    )

    admin_login = expect_success(
        "admin.login",
        send(args.host, args.port, {"action": "admin.login", "username": "admin", "password": "admin123"}),
    )
    admin_token = admin_login["session"]["token"]

    expect_failure(
        "bad admin.login",
        send(args.host, args.port, {"action": "admin.login", "username": "admin", "password": "bad"}),
    )

    expect_success(
        "user.profile",
        send(args.host, args.port, {"action": "user.profile", "session_token": user_token}),
    )

    expect_success(
        "user.update_profile",
        send(
            args.host,
            args.port,
            {"action": "user.update_profile", "session_token": user_token, "nickname": "Demo User"},
        ),
    )

    expect_success(
        "admin.user.list",
        send(args.host, args.port, {"action": "admin.user.list", "session_token": admin_token}),
    )

    expect_success(
        "admin.user.set_status frozen",
        send(
            args.host,
            args.port,
            {"action": "admin.user.set_status", "session_token": admin_token, "user_id": 3, "status": "frozen"},
        ),
    )
    expect_failure(
        "frozen by admin user.login",
        send(args.host, args.port, {"action": "user.login", "phone": "13800000003"}),
    )
    expect_success(
        "admin.user.set_status active",
        send(
            args.host,
            args.port,
            {"action": "admin.user.set_status", "session_token": admin_token, "user_id": 3, "status": "active"},
        ),
    )

    expect_success("station.list", send(args.host, args.port, {"action": "station.list"}))
    expect_success("charger.list", send(args.host, args.port, {"action": "charger.list", "station_id": 1}))

    suffix = int(time.time())
    station = expect_success(
        "admin.station.create",
        send(
            args.host,
            args.port,
            {
                "action": "admin.station.create",
                "session_token": admin_token,
                "name": f"Smoke Test Station {suffix}",
                "address": "Smoke Test Road",
                "latitude": 39.9,
                "longitude": 116.3,
                "status": "open",
            },
        ),
    )["station"]
    station_id = station["id"]

    expect_success(
        "admin.station.update",
        send(
            args.host,
            args.port,
            {
                "action": "admin.station.update",
                "session_token": admin_token,
                "station_id": station_id,
                "name": f"Smoke Test Station Updated {suffix}",
                "address": "Smoke Test Road Updated",
                "latitude": 39.91,
                "longitude": 116.31,
                "status": "open",
            },
        ),
    )

    expect_success(
        "admin.station.set_status closed",
        send(
            args.host,
            args.port,
            {
                "action": "admin.station.set_status",
                "session_token": admin_token,
                "station_id": station_id,
                "status": "closed",
            },
        ),
    )
    expect_success(
        "admin.station.set_status open",
        send(
            args.host,
            args.port,
            {
                "action": "admin.station.set_status",
                "session_token": admin_token,
                "station_id": station_id,
                "status": "open",
            },
        ),
    )

    charger_code = f"SMOKE-F-{suffix}"
    charger = expect_success(
        "admin.charger.create",
        send(
            args.host,
            args.port,
            {
                "action": "admin.charger.create",
                "session_token": admin_token,
                "station_id": station_id,
                "code": charger_code,
                "type": "fast",
                "power_kw": 90,
                "status": "idle",
            },
        ),
    )["charger"]
    charger_id = charger["id"]

    expect_success(
        "admin.charger.list",
        send(
            args.host,
            args.port,
            {"action": "admin.charger.list", "session_token": admin_token, "station_id": station_id},
        ),
    )

    expect_success(
        "admin.charger.update",
        send(
            args.host,
            args.port,
            {
                "action": "admin.charger.update",
                "session_token": admin_token,
                "charger_id": charger_id,
                "station_id": station_id,
                "code": charger_code,
                "type": "fast",
                "power_kw": 120,
                "status": "fault",
            },
        ),
    )

    expect_success(
        "admin.charger.set_status idle",
        send(
            args.host,
            args.port,
            {
                "action": "admin.charger.set_status",
                "session_token": admin_token,
                "charger_id": charger_id,
                "status": "idle",
            },
        ),
    )

    expect_success(
        "balance.recharge",
        send(args.host, args.port, {"action": "balance.recharge", "session_token": user_token, "amount_cents": 1000}),
    )

    reserved = expect_success(
        "order.create for cancel",
        send(args.host, args.port, {"action": "order.create", "session_token": user_token, "charger_id": charger_id}),
    )["order"]
    expect_success(
        "order.cancel",
        send(args.host, args.port, {"action": "order.cancel", "session_token": user_token, "order_id": reserved["id"]}),
    )

    active_order = expect_success(
        "order.create for settlement",
        send(args.host, args.port, {"action": "order.create", "session_token": user_token, "charger_id": charger_id}),
    )["order"]
    order_id = active_order["id"]
    expect_success(
        "order.current reserved",
        send(args.host, args.port, {"action": "order.current", "session_token": user_token}),
    )
    expect_success(
        "order.start",
        send(args.host, args.port, {"action": "order.start", "session_token": user_token, "order_id": order_id}),
    )
    expect_success(
        "telemetry.report",
        send(
            args.host,
            args.port,
            {
                "action": "telemetry.report",
                "charger_id": charger_id,
                "status": "charging",
                "power_kw": 88.5,
                "energy_kwh": 0.8,
            },
        ),
    )
    expect_success(
        "order.stop",
        send(
            args.host,
            args.port,
            {"action": "order.stop", "session_token": user_token, "order_id": order_id, "energy_kwh": 1.5},
        ),
    )
    expect_success(
        "order.settle",
        send(args.host, args.port, {"action": "order.settle", "session_token": user_token, "order_id": order_id}),
    )
    expect_success(
        "order.list",
        send(args.host, args.port, {"action": "order.list", "session_token": user_token, "limit": 5}),
    )
    expect_success(
        "balance.logs",
        send(args.host, args.port, {"action": "balance.logs", "session_token": user_token, "limit": 5}),
    )
    expect_success(
        "telemetry.list",
        send(args.host, args.port, {"action": "telemetry.list", "charger_id": charger_id, "limit": 5}),
    )
    expect_success(
        "statistics.overview",
        send(
            args.host,
            args.port,
            {"action": "statistics.overview", "session_token": admin_token, "station_id": station_id},
        ),
    )
    load_history = expect_success(
        "statistics.load_history",
        send(
            args.host,
            args.port,
            {"action": "statistics.load_history", "session_token": admin_token, "station_id": station_id},
        ),
    )
    if not load_history["samples"] or "actual_load_kw" not in load_history["samples"][0]:
        raise AssertionError(f"statistics.load_history returned no usable telemetry sample: {load_history}")
    print("[OK] statistics.load_history returns actual load samples")
    expect_success(
        "forecast.generate",
        send(
            args.host,
            args.port,
            {
                "action": "forecast.generate",
                "session_token": admin_token,
                "station_id": station_id,
                "horizon_hours": 1,
            },
        ),
    )
    expect_success(
        "forecast.list",
        send(
            args.host,
            args.port,
            {
                "action": "forecast.list",
                "session_token": admin_token,
                "station_id": station_id,
                "horizon_hours": 1,
                "limit": 5,
            },
        ),
    )

    expect_success(
        "admin.charger.set_status offline",
        send(
            args.host,
            args.port,
            {
                "action": "admin.charger.set_status",
                "session_token": admin_token,
                "charger_id": charger_id,
                "status": "offline",
            },
        ),
    )

    expect_success(
        "admin.station.set_status final closed",
        send(
            args.host,
            args.port,
            {
                "action": "admin.station.set_status",
                "session_token": admin_token,
                "station_id": station_id,
                "status": "closed",
            },
        ),
    )

    print("Phase 0/1/2/3/extension smoke test passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
