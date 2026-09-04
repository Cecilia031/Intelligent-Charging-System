# Ubuntu Build Guide

Target environment:

- Ubuntu 22.04
- CMake 3.16+
- C++17 compiler
- Qt 5 or Qt 6 with Core, Network and Sql modules
- SQLite Qt driver

Install common packages for Qt 5:

```bash
sudo apt update
sudo apt install -y build-essential cmake qtbase5-dev libqt5sql5-sqlite python3
```

Install common packages for Qt 6:

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev libqt6sql6-sqlite
```

Configure and build:

```bash
cmake -S . -B build-qt5
cmake --build build-qt5 -j"$(nproc)"
```

If you prefer opening the project in Qt Creator 5, open `charging_server.pro`.
For the user client, open `user_client.pro`. For the admin client, open `admin_client.pro`.

qmake command-line build:

```bash
mkdir -p build-qmake
cd build-qmake
qmake ../charging_server.pro
make -j"$(nproc)"
cd ..
```

User client qmake build:

```bash
mkdir -p build-user-qmake
cd build-user-qmake
qmake ../user_client.pro
make -j"$(nproc)"
cd ..
```

Admin client qmake build:

```bash
mkdir -p build-admin-qmake
cd build-admin-qmake
qmake ../admin_client.pro
make -j"$(nproc)"
cd ..
```

Run the server:

```bash
./build-qt5/charging_server --port 45454 --db data/charging.sqlite --schema resources/schema.sql --seed resources/seed.sql
```

If built with qmake, run:

```bash
./build-qmake/charging_server --port 45454 --db data/charging.sqlite --schema resources/schema.sql --seed resources/seed.sql
```

Run the user client:

```bash
./build-user-qmake/user_client
```

Run the admin client:

```bash
./build-admin-qmake/admin_client
```

The server creates the `data` directory if needed. Runtime database files should not be committed.

Quick protocol check:

```bash
python3 tools/send_json.py '{"action":"ping"}'
python3 tools/send_json.py '{"action":"user.login","phone":"13800000001"}'
python3 tools/send_json.py '{"action":"admin.login","username":"admin","password":"admin123"}'
python3 tools/send_json.py '{"action":"station.list"}'
python3 tools/send_json.py '{"action":"charger.list","station_id":1}'
python3 tools/send_json.py '{"action":"order.create","user_id":1,"charger_id":1}'
python3 tools/send_json.py '{"action":"order.start","order_id":1}'
python3 tools/send_json.py '{"action":"order.stop","order_id":1,"energy_kwh":10.5}'
python3 tools/send_json.py '{"action":"order.settle","order_id":1}'
python3 tools/send_json.py '{"action":"order.list","user_id":1}'
```

Phase 0/1/2/3 smoke test:

```bash
python3 tools/smoke_test.py
```

Phase 4 checks:

```bash
./build-qmake/charging_server --init-only --db data/check.sqlite --schema resources/schema.sql --seed resources/seed.sql
python3 tools/invalid_request_test.py
python3 tools/concurrency_test.py --clients 20
```

See [TEST_PLAN.md](TEST_PLAN.md) for the full pre-push checklist.
