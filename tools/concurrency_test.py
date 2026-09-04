#!/usr/bin/env python3
import argparse
import json
import socket
from concurrent.futures import ThreadPoolExecutor, as_completed


def send(host: str, port: int, index: int) -> dict:
    payload = {"action": "ping", "request_id": index}
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.sendall(json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n")
        line = sock.makefile("rb").readline()
    if not line:
        raise RuntimeError(f"request {index} received empty response")
    response = json.loads(line.decode("utf-8"))
    if not response.get("success") or response.get("request_id") != index:
        raise RuntimeError(f"request {index} failed: {response}")
    return response


def main() -> int:
    parser = argparse.ArgumentParser(description="Run simple concurrent ping checks.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=45454)
    parser.add_argument("--clients", type=int, default=20)
    args = parser.parse_args()

    with ThreadPoolExecutor(max_workers=args.clients) as pool:
        futures = [pool.submit(send, args.host, args.port, index) for index in range(args.clients)]
        for future in as_completed(futures):
            future.result()

    print(f"concurrency test passed: {args.clients} clients")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
