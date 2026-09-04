#!/usr/bin/env python3
"""Run Phase 7 authorization and order-state regression checks."""

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


def admin_payload(token: str, action: str, **fields: object) -> dict:
    return {"action": action, "session_token": token, **fields}


def user_payload(token: str, action: str, **fields: object) -> dict:
    return {"action": action, "session_token": token, **fields}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run authorization, business-rule and state-machine regression checks."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=45454)
    args = parser.parse_args()

    host = args.host
    port = args.port
    user_one = expect_success(
        "active user login",
        send(host, port, {"action": "user.login", "phone": "13800000001"}),
    )
    user_one_token = user_one["session"]["token"]
    user_one_id = user_one["user"]["id"]
    user_three = expect_success(
        "second active user login",
        send(host, port, {"action": "user.login", "phone": "13800000003"}),
    )
    user_three_token = user_three["session"]["token"]
    admin = expect_success(
        "admin login",
        send(host, port, {"action": "admin.login", "username": "admin", "password": "admin123"}),
    )
    admin_token = admin["session"]["token"]

    expect_failure(
        "anonymous admin endpoint",
        send(host, port, {"action": "admin.user.list"}),
    )
    expect_failure(
        "anonymous load history",
        send(host, port, {"action": "statistics.load_history"}),
    )
    expect_failure(
        "user cannot change account status",
        send(
            host,
            port,
            user_payload(
                user_one_token,
                "admin.user.set_status",
                user_id=3,
                status="frozen",
            ),
        ),
    )
    expect_failure(
        "anonymous order list",
        send(host, port, {"action": "order.list"}),
    )
    expect_failure(
        "anonymous balance logs",
        send(host, port, {"action": "balance.logs"}),
    )
    expect_failure(
        "anonymous reservation using user id",
        send(host, port, {"action": "order.create", "user_id": 1, "charger_id": 1}),
    )
    expect_failure(
        "anonymous recharge using user id",
        send(host, port, {"action": "balance.recharge", "user_id": 1, "amount": 1}),
    )
    expect_failure(
        "admin cannot create user reservation",
        send(host, port, admin_payload(admin_token, "order.create", user_id=1, charger_id=1)),
    )
    expect_failure(
        "admin cannot recharge user balance",
        send(host, port, admin_payload(admin_token, "balance.recharge", user_id=1, amount=1)),
    )
    own_orders = expect_success(
        "user order list is scoped to owner",
        send(host, port, user_payload(user_one_token, "order.list")),
    )
    if any(order["user_id"] != user_one_id for order in own_orders["orders"]):
        raise AssertionError(f"user order list leaked another user's order: {own_orders}")
    print("[OK] user order list contains only the owner")
    own_logs = expect_success(
        "user balance logs are scoped to owner",
        send(host, port, user_payload(user_one_token, "balance.logs")),
    )
    if any(log["user_id"] != user_one_id for log in own_logs["logs"]):
        raise AssertionError(f"user balance logs leaked another user's log: {own_logs}")
    print("[OK] user balance logs contain only the owner")
    expect_failure(
        "fault charger cannot reserve",
        send(host, port, user_payload(user_one_token, "order.create", charger_id=5)),
    )
    expect_failure(
        "offline charger cannot reserve",
        send(host, port, user_payload(user_one_token, "order.create", charger_id=6)),
    )

    suffix = int(time.time() * 1000)
    closed_station = expect_success(
        "create closed station",
        send(
            host,
            port,
            admin_payload(
                admin_token,
                "admin.station.create",
                name=f"Regression Closed Station {suffix}",
                address="Regression Test Road",
                latitude=39.9,
                longitude=116.3,
                status="closed",
            ),
        ),
    )["station"]
    closed_charger = expect_success(
        "create charger at closed station",
        send(
            host,
            port,
            admin_payload(
                admin_token,
                "admin.charger.create",
                station_id=closed_station["id"],
                code=f"REG-C-{suffix}",
                type="fast",
                power_kw=60,
                status="idle",
            ),
        ),
    )["charger"]
    expect_failure(
        "closed station cannot reserve",
        send(
            host,
            port,
            user_payload(user_one_token, "order.create", charger_id=closed_charger["id"]),
        ),
    )

    open_station = expect_success(
        "create open station",
        send(
            host,
            port,
            admin_payload(
                admin_token,
                "admin.station.create",
                name=f"Regression Open Station {suffix}",
                address="Regression Test Road",
                latitude=39.91,
                longitude=116.31,
                status="open",
            ),
        ),
    )["station"]
    charger = expect_success(
        "create available charger",
        send(
            host,
            port,
            admin_payload(
                admin_token,
                "admin.charger.create",
                station_id=open_station["id"],
                code=f"REG-O-{suffix}",
                type="fast",
                power_kw=60,
                status="idle",
            ),
        ),
    )["charger"]

    order = expect_success(
        "create reservation",
        send(host, port, user_payload(user_one_token, "order.create", charger_id=charger["id"])),
    )["order"]
    order_id = order["id"]
    expect_failure(
        "duplicate reservation blocked",
        send(host, port, user_payload(user_three_token, "order.create", charger_id=charger["id"])),
    )
    expect_failure(
        "other user cannot start order",
        send(host, port, user_payload(user_three_token, "order.start", order_id=order_id)),
    )
    expect_failure(
        "reserved order cannot settle",
        send(host, port, user_payload(user_one_token, "order.settle", order_id=order_id)),
    )

    expect_success(
        "start reserved order",
        send(host, port, user_payload(user_one_token, "order.start", order_id=order_id)),
    )
    expect_failure(
        "charging order cannot start twice",
        send(host, port, user_payload(user_one_token, "order.start", order_id=order_id)),
    )
    expect_failure(
        "charging order cannot cancel",
        send(host, port, user_payload(user_one_token, "order.cancel", order_id=order_id)),
    )
    expect_success(
        "stop charging order",
        send(
            host,
            port,
            user_payload(user_one_token, "order.stop", order_id=order_id, energy_kwh=2.5),
        ),
    )
    settled = expect_success(
        "settle stopped order",
        send(host, port, user_payload(user_one_token, "order.settle", order_id=order_id)),
    )
    if settled["order"]["amount_cents"] != 300:
        raise AssertionError(f"unexpected settlement amount: {settled}")
    print("[OK] settlement billing rule: 2.5 kWh = 300 cents")
    expect_failure(
        "completed order cannot settle twice",
        send(host, port, user_payload(user_one_token, "order.settle", order_id=order_id)),
    )

    admin_order = expect_success(
        "create reservation for admin management",
        send(host, port, user_payload(user_one_token, "order.create", charger_id=charger["id"])),
    )["order"]
    admin_order_id = admin_order["id"]
    expect_success(
        "admin can start user order",
        send(host, port, admin_payload(admin_token, "order.start", order_id=admin_order_id)),
    )
    expect_success(
        "admin can stop user order",
        send(
            host,
            port,
            admin_payload(admin_token, "order.stop", order_id=admin_order_id, energy_kwh=1.0),
        ),
    )
    expect_success(
        "admin can settle user order",
        send(host, port, admin_payload(admin_token, "order.settle", order_id=admin_order_id)),
    )
    admin_orders = expect_success(
        "admin can list all orders",
        send(host, port, admin_payload(admin_token, "order.list")),
    )
    if len(admin_orders["orders"]) < 2:
        raise AssertionError(f"admin order list did not include expected rows: {admin_orders}")
    print("[OK] admin order list contains managed orders")

    print("Phase 7 regression test passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
