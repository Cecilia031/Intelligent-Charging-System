#!/usr/bin/env python3
"""Check that simple TCP edge cases do not break the server."""

import argparse
import json
import socket
import sys


def read_json_line(file_obj) -> dict:
    line = file_obj.readline()
    if not line:
        raise RuntimeError("server closed connection without a response")
    return json.loads(line.decode("utf-8"))


def assert_success(name: str, response: dict) -> None:
    if not response.get("success"):
        raise AssertionError(f"{name} failed: {response}")
    print(f"[OK] {name}")


def assert_failure(name: str, response: dict, error_code: str) -> None:
    if response.get("success") or response.get("error_code") != error_code:
        raise AssertionError(f"{name} failed expectation: {response}")
    print(f"[OK] {name}: {error_code}")


def send_one(host: str, port: int, payload: dict) -> dict:
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n")
        return read_json_line(sock.makefile("rb"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Run TCP connection resilience checks.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=45454)
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        file_obj = sock.makefile("rb")
        sock.sendall(b"\n")
        sock.sendall(b'{"action":"ping","request_id":"first"}\n')
        first = read_json_line(file_obj)
        if first.get("request_id") != "first":
            raise AssertionError(f"request_id was not echoed: {first}")
        assert_success("blank line ignored and first ping handled", first)

        sock.sendall(b"{bad json\n")
        assert_failure("invalid line on persistent connection", read_json_line(file_obj), "INVALID_JSON")

        sock.sendall(b'{"action":"ping","request_id":"second"}\n')
        second = read_json_line(file_obj)
        if second.get("request_id") != "second":
            raise AssertionError(f"second request_id was not echoed: {second}")
        assert_success("connection stays usable after invalid line", second)

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        sock.sendall(b'{"action":"ping"')

    assert_success("server responds after client disconnect", send_one(args.host, args.port, {"action": "ping"}))
    print("network resilience test passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
