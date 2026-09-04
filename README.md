# Intelligent-Charging-System

The 11th group's project in the 2026-2027 fall semester short term.

This repository currently contains the Qt/C++ service baseline for the intelligent electric vehicle charging platform.

## Build

See [docs/UBUNTU_BUILD.md](docs/UBUNTU_BUILD.md).

For Qt Creator 5, open `charging_server.pro` for the server, `user_client.pro` for the user client, or `admin_client.pro` for the admin client.

## Protocol

See [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Pre-Push Check

On Ubuntu, run the Phase 7 check before pushing or merging:

```bash
bash tools/phase7_check.sh
```
