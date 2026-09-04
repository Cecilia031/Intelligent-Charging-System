#!/usr/bin/env python3
import argparse
import json
import socket


def send_raw(host: str, port: int, raw: bytes) -> dict:
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(raw + b"\n")
        line = sock.makefile("rb").readline()
    if not line:
        raise RuntimeError("server returned empty response")
    return json.loads(line.decode("utf-8"))


def assert_failure(name: str, response: dict, error_code: str) -> None:
    if response.get("success") or response.get("error_code") != error_code:
        raise AssertionError(f"{name} failed expectation: {response}")
    print(f"[OK] {name}: {error_code}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check invalid request handling.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=45454)
    args = parser.parse_args()

    assert_failure(
        "invalid JSON",
        send_raw(args.host, args.port, b"{bad json"),
        "INVALID_JSON",
    )
    assert_failure(
        "unsupported action",
        send_raw(args.host, args.port, b'{"action":"missing.action"}'),
        "UNSUPPORTED_ACTION",
    )

    print("invalid request test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
