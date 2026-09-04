#!/usr/bin/env python3
import argparse
import socket
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description="Send one JSON line to the charging server.")
    parser.add_argument("json_line", help="JSON request, for example: '{\"action\":\"ping\"}'")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=45454)
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        sock.sendall(args.json_line.encode("utf-8") + b"\n")
        response = sock.makefile("rb").readline()

    sys.stdout.write(response.decode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
