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

The Phase 9 Dashboard works without Qt WebEngine by showing the generated HTML
source and allowing export. To embed the ECharts page inside the admin client,
install Qt WebEngine as an optional dependency:

```bash
sudo apt install -y qtwebengine5-dev libqt5webengine5 libqt5webenginewidgets5
```

If an Ubuntu release does not provide one of these package names, keep the
base Qt 5 build: the admin client will compile in HTML preview mode.

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
The user and admin interfaces are stored as Designer files:

- `src/user_client/user_client.ui`
- `src/admin_client/admin_client.ui`

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

When running from Qt Creator with a shadow build directory, relative paths may point to the build directory. The server now tries to locate `resources/schema.sql` and `resources/seed.sql` from the source project automatically. You can also set absolute run arguments in Qt Creator:

```bash
--port 45454 --db /tmp/charging.sqlite --schema /mnt/hgfs/K1/Intelligent-Charging-System/resources/schema.sql --seed /mnt/hgfs/K1/Intelligent-Charging-System/resources/seed.sql
```

Run the user client:

```bash
./build-user-qmake/user_client
```

Run the admin client:

```bash
./build-admin-qmake/admin_client
```

Phase 9 dashboard check:

1. Log in to the admin client and open the `Dashboard` tab.
2. Click `Refresh Dashboard`; it reads the existing statistics and forecast
   protocol data for the selected station and forecast horizon.
3. With `Qt WebEngineWidgets` installed, the tab embeds an ECharts dashboard.
   Without it, the generated HTML appears in the read-only preview and can be
   saved with `Export HTML`.
4. The HTML loads ECharts from jsDelivr. If the Linux environment cannot reach
   that CDN, metric cards and the raw data remain visible and the chart areas
   show a readable network warning instead of a blank page.

The server creates the `data` directory if needed. Runtime database files should not be committed.

Quick protocol check:

```bash
python3 tools/send_json.py '{"action":"ping"}'
python3 tools/send_json.py '{"action":"user.login","phone":"13800000001"}'
python3 tools/send_json.py '{"action":"admin.login","username":"admin","password":"admin123"}'
python3 tools/send_json.py '{"action":"station.list"}'
python3 tools/send_json.py '{"action":"charger.list","station_id":1}'
```

For user operations, replace `YOUR_USER_TOKEN` below with the token returned by
`user.login`:

```bash
python3 tools/send_json.py '{"action":"order.create","session_token":"YOUR_USER_TOKEN","charger_id":1}'
python3 tools/send_json.py '{"action":"order.start","session_token":"YOUR_USER_TOKEN","order_id":1}'
python3 tools/send_json.py '{"action":"order.stop","session_token":"YOUR_USER_TOKEN","order_id":1,"energy_kwh":10.5}'
python3 tools/send_json.py '{"action":"order.settle","session_token":"YOUR_USER_TOKEN","order_id":1}'
python3 tools/send_json.py '{"action":"order.list","session_token":"YOUR_USER_TOKEN"}'
```

Full protocol smoke test:

```bash
python3 tools/smoke_test.py
```

One-command Phase 7 pre-push check:

```bash
bash tools/phase7_check.sh
```

Individual Phase 7 checks:

```bash
./build-qmake/charging_server --init-only --db data/check.sqlite --schema resources/schema.sql --seed resources/seed.sql
python3 tools/invalid_request_test.py
python3 tools/network_resilience_test.py
python3 tools/concurrency_test.py --clients 20
python3 tools/regression_test.py
```

See [TEST_PLAN.md](TEST_PLAN.md) for the full pre-push checklist.
