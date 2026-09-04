# Phase 4 Test Plan

Run these checks before pushing code or before merging a teammate's branch.

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
Phase 0/1/2/3 smoke test passed.
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

## 7. Client Self-Check

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

## 8. Manual Acceptance Checklist

- Qt Creator 5 can open `charging_server.pro`.
- Qt Creator 5 can open `user_client.pro`.
- Qt Creator 5 can open `admin_client.pro`.
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
