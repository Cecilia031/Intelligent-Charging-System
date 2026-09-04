# Phase 7 Acceptance Record

Phase 7 closes the stage A integration baseline. It focuses on repeatable
Ubuntu builds, full protocol regression, network resilience, authorization
checks and manual client verification.

## Scope

Completed scope:

- Server, user client and admin client can be built from qmake project files.
- User and admin interfaces are stored in Qt Designer `.ui` files.
- Core protocol tests cover login, station and charger queries, order lifecycle,
  recharge, balance logs, telemetry, statistics and forecast endpoints.
- Invalid JSON and unsupported actions return structured error responses.
- Concurrent TCP clients can send health checks without breaking one another.
- Persistent TCP connections can carry multiple requests; blank lines and invalid
  lines do not break later valid requests on the same connection.
- Anonymous access is rejected for order lists, balance logs and user operations.
- User-side operations require a valid user session token.
- Users can only access their own orders and balance logs.
- Administrators can manage users, stations, chargers and order status.
- Closed stations, fault chargers and offline chargers cannot accept new orders.
- Invalid order transitions and duplicate settlement are rejected.
- Settlement updates order, balance, balance logs and charger totals in one
  database transaction.

## Ubuntu Acceptance Command

Run from the repository root:

```bash
bash tools/phase7_check.sh
```

Expected final line:

```text
[7/7] Phase 7 checks passed
```

The script performs:

1. qmake build for `charging_server.pro`.
2. qmake build for `user_client.pro`.
3. qmake build for `admin_client.pro`.
4. Temporary SQLite database initialization.
5. Server startup on port `45454`.
6. `tools/smoke_test.py`.
7. `tools/invalid_request_test.py`.
8. `tools/network_resilience_test.py`.
9. `tools/concurrency_test.py --clients 20`.
10. `tools/regression_test.py`.

## Manual Client Acceptance

User client:

- Login with `13800000001`.
- Refresh station and charger lists.
- Reserve an idle charger.
- Start, stop and settle the order.
- Refresh current order, order history, balance and balance logs.
- Recharge balance and confirm the updated value is shown.

Admin client:

- Login with `admin / admin123`.
- Refresh users and toggle one test user between frozen and active.
- Create or update one station.
- Create or update one charger.
- Refresh orders and telemetry.
- Open the Overview tab, refresh operational totals, generate a 1/6/24 hour
  forecast and refresh forecast records.

## Known Limits

- Windows in this workspace does not provide `qmake` or `bash`; final build and
  `phase7_check.sh` must be run inside Ubuntu.
- `forecast.generate` uses a simple explainable demo baseline, not a production
  machine learning model.
- ECharts and Qt `QWebEngineView` large-screen integration are Phase 9 extension
  work and are not required for Phase 7 closure.
