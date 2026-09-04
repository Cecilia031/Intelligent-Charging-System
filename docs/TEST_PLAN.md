# Phase 7 Test Plan

Run these checks before pushing code or before merging a teammate's branch.

## 0. One-Command Ubuntu Check

From the repository root:

```bash
bash tools/phase7_check.sh
```

Expected result:

```text
[7/7] Phase 7 checks passed
```

The script builds the server, user client and admin client with qmake, initializes
a temporary SQLite database, starts the server, runs all protocol checks, then
stops the server automatically.

Record the result with [PHASE7_ACCEPTANCE.md](PHASE7_ACCEPTANCE.md) when preparing
the stage A handoff.

## 1. Build

qmake:

```bash
mkdir -p build-qmake
cd build-qmake
qmake ../charging_server.pro
make -j"$(nproc)"
cd ..
```

User client qmake:

```bash
mkdir -p build-user-qmake
cd build-user-qmake
qmake ../user_client.pro
make -j"$(nproc)"
cd ..
```

Admin client qmake:

```bash
mkdir -p build-admin-qmake
cd build-admin-qmake
qmake ../admin_client.pro
make -j"$(nproc)"
cd ..
```

CMake:

```bash
cmake -S . -B build-qt5
cmake --build build-qt5 -j"$(nproc)"
```

## 2. Database Initialization

```bash
./build-qmake/charging_server --init-only --db data/check.sqlite --schema resources/schema.sql --seed resources/seed.sql
```

Expected result: exits with code `0` and prints `database initialized`.

## 3. Start Server

```bash
./build-qmake/charging_server --port 45454 --db data/charging.sqlite --schema resources/schema.sql --seed resources/seed.sql
```

Expected result: logs the database path and listening port.

## 4. Protocol Smoke Test

```bash
python3 tools/smoke_test.py
```

Expected result:

```text
Phase 0/1/2/3/extension smoke test passed.
```

## 5. Invalid Request Handling

```bash
python3 tools/invalid_request_test.py
```

Expected result:

```text
invalid request test passed
```

## 6. Concurrent Connections

```bash
python3 tools/concurrency_test.py --clients 20
```

Expected result:

```text
concurrency test passed: 20 clients
```

## 7. Network Resilience

```bash
python3 tools/network_resilience_test.py
```

Expected result:

```text
network resilience test passed
```

It verifies blank lines are ignored, one TCP connection can carry multiple JSON
requests, invalid JSON does not break that connection, and a client disconnecting
mid-request does not stop the server from serving later clients.

## 8. Phase 7 Regression Checks

Run this against a newly initialized database. The script creates test stations,
chargers and orders, so it is intentionally not run against a presentation database.

```bash
python3 tools/regression_test.py
```

Expected result:

```text
Phase 7 regression test passed.
```

It verifies anonymous and cross-user access is rejected, closed/fault/offline
equipment cannot accept reservations, duplicate reservations are blocked, invalid
order state transitions fail, and the settlement billing rule remains correct.

## 9. Client Self-Check

User client:

- Login with `13800000001`
- Select a station, then a charger
- Reserve, start, stop and settle one order
- Refresh current order and balance logs

Admin client:

- Login with `admin / admin123`
- Refresh users and freeze one active user, then restore it
- Create or update one station
- Create or update one charger
- Refresh orders and telemetry

## 10. Manual Acceptance Checklist

- Qt Creator 5 can open `charging_server.pro`.
- Qt Creator 5 can open `user_client.pro`, and `src/user_client/user_client.ui` opens in Designer.
- Qt Creator 5 can open `admin_client.pro`, and `src/admin_client/admin_client.ui` opens in Designer.
- Wrong admin password fails.
- Frozen user cannot log in.
- Closed station is hidden by default `station.list`.
- Fault/offline chargers cannot create orders.
- Reserved order can be cancelled.
- Charging order can be stopped and settled.
- Settlement deducts balance and creates a balance log.
- Server logs every request action, success flag and elapsed time.
- User client can login, refresh stations, refresh chargers, reserve, start, stop, settle, recharge and show order history.
- Admin client can login, list users, freeze/activate users, manage stations, manage chargers, view orders and inspect telemetry.
- Admin client Overview tab can refresh operational totals, generate a 1/6/24 hour forecast, and list stored forecast records.
- Admin client Dashboard tab can refresh the order-status and actual-versus-forecast charts; without Qt WebEngine it can export the generated HTML.
- `statistics.load_history` rejects anonymous requests and returns chronological actual-load samples for an administrator.
- `tools/network_resilience_test.py` passes against a running server.
- `tools/regression_test.py` passes against a newly initialized database.
