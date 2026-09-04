# TCP JSON Protocol

The service uses one JSON request per TCP line and returns one JSON response per line.

Default port:

```text
45454
```

Common response fields:

```json
{
  "success": true,
  "action": "ping",
  "server_time": "2026-09-03T08:00:00.000Z",
  "data": {}
}
```

Failed requests return:

```json
{
  "success": false,
  "action": "user.login",
  "server_time": "2026-09-03T08:00:00.000Z",
  "error_code": "REQUEST_FAILED",
  "error": "phone is required"
}
```

Invalid JSON uses `error_code = "INVALID_JSON"`. Unsupported actions use `error_code = "UNSUPPORTED_ACTION"`.

If a request includes `request_id`, the server echoes it in the response.

## `ping`

Request:

```json
{"action":"ping"}
```

Success data:

```json
{"status":"ok"}
```

## `user.login`

Request:

```json
{"action":"user.login","phone":"13800000001"}
```

Success data:

```json
{
  "session": {
    "token": "user-1-1788412345678-abc123",
    "expires_at": "2026-09-03T16:00:00.000Z"
  },
  "user": {
    "id": 1,
    "phone": "13800000001",
    "nickname": "Demo User",
    "avatar_url": "",
    "balance_cents": 12000,
    "balance_yuan": 120,
    "status": "active"
  }
}
```

Seed users:

- `13800000001`: active
- `13800000002`: frozen
- `13800000003`: active with low balance

## `admin.login`

Request:

```json
{"action":"admin.login","username":"admin","password":"admin123"}
```

Success data:

```json
{
  "session": {
    "token": "admin-1-1788412345678-abc123",
    "expires_at": "2026-09-03T16:00:00.000Z"
  },
  "admin": {
    "id": 1,
    "username": "admin",
    "display_name": "System Administrator",
    "status": "active"
  }
}
```

The database stores a salted SHA-256 digest for demo use instead of a plain text password.

## `user.profile`

Request:

```json
{"action":"user.profile","session_token":"user-1-1788412345678-abc123"}
```

Returns the current user's profile. The token must come from `user.login`.

## `user.update_profile`

Request:

```json
{
  "action": "user.update_profile",
  "session_token": "user-1-1788412345678-abc123",
  "nickname": "Demo User",
  "avatar_url": "https://example.com/avatar.png"
}
```

At least one of `nickname` or `avatar_url` is required.

## `admin.user.list`

Request:

```json
{"action":"admin.user.list","session_token":"admin-1-1788412345678-abc123"}
```

Optional fields:

- `keyword`: filters by phone or nickname.
- `status`: `active` or `frozen`.
- `limit`: defaults to `100`, maximum `200`.

## `admin.user.set_status`

Request:

```json
{
  "action": "admin.user.set_status",
  "session_token": "admin-1-1788412345678-abc123",
  "user_id": 2,
  "status": "active"
}
```

Allowed status values are `active` and `frozen`. Frozen users cannot log in or create new charging orders.

## `station.list`

Request:

```json
{"action":"station.list"}
```

Optional fields:

- `keyword`: filters by station name or address.
- `include_closed`: includes closed stations when true.

Success data:

```json
{
  "stations": [
    {
      "id": 1,
      "name": "Software Park Charging Station",
      "address": "No. 1 Software Park Road",
      "latitude": 39.9838,
      "longitude": 116.3159,
      "status": "open"
    }
  ]
}
```

## `admin.station.create`

Request:

```json
{
  "action": "admin.station.create",
  "session_token": "admin-1-1788412345678-abc123",
  "name": "New Station",
  "address": "No. 9 Demo Road",
  "latitude": 39.9,
  "longitude": 116.3,
  "status": "open"
}
```

Creates a station. `status` must be `open` or `closed`.

## `admin.station.update`

Request:

```json
{
  "action": "admin.station.update",
  "session_token": "admin-1-1788412345678-abc123",
  "station_id": 1,
  "name": "Updated Station",
  "address": "Updated Address",
  "latitude": 39.91,
  "longitude": 116.31,
  "status": "open"
}
```

Updates all editable station fields.

## `admin.station.set_status`

Request:

```json
{
  "action": "admin.station.set_status",
  "session_token": "admin-1-1788412345678-abc123",
  "station_id": 1,
  "status": "closed"
}
```

Closed stations are hidden from default `station.list` responses and cannot accept new `order.create` requests.

## `charger.list`

Request:

```json
{"action":"charger.list","station_id":1}
```

Success data:

```json
{
  "chargers": [
    {
      "id": 1,
      "station_id": 1,
      "station_name": "Software Park Charging Station",
      "code": "SP-F-001",
      "type": "fast",
      "power_kw": 120,
      "status": "idle",
      "current_power_kw": 0,
      "total_orders": 0,
      "total_energy_kwh": 0,
      "total_duration_minutes": 0
    }
  ]
}
```

## `admin.charger.list`

Request:

```json
{"action":"admin.charger.list","session_token":"admin-1-1788412345678-abc123"}
```

Optional fields:

- `station_id`: filters by station.
- `status`: filters by `idle`, `charging`, `fault` or `offline`.
- `limit`: defaults to `200`, maximum `500`.

## `admin.charger.create`

Request:

```json
{
  "action": "admin.charger.create",
  "session_token": "admin-1-1788412345678-abc123",
  "station_id": 1,
  "code": "SP-F-003",
  "type": "fast",
  "power_kw": 120,
  "status": "idle"
}
```

`code` must be unique. `type` must be `fast` or `slow`. New chargers cannot be created directly as `charging`.

## `admin.charger.update`

Request:

```json
{
  "action": "admin.charger.update",
  "session_token": "admin-1-1788412345678-abc123",
  "charger_id": 1,
  "station_id": 1,
  "code": "SP-F-001",
  "type": "fast",
  "power_kw": 120,
  "status": "idle"
}
```

Updates station ownership, code, type, power and status. Chargers with active `reserved` or `charging` orders cannot be moved to a non-`charging` status.

## `admin.charger.set_status`

Request:

```json
{
  "action": "admin.charger.set_status",
  "session_token": "admin-1-1788412345678-abc123",
  "charger_id": 1,
  "status": "fault"
}
```

Allowed status values are `idle`, `charging`, `fault` and `offline`. Fault or offline chargers cannot be selected by `order.create`.

## `order.create`

Request:

```json
{"action":"order.create","session_token":"user-1-1788412345678-abc123","charger_id":1}
```

Success data:

```json
{
  "order": {
    "id": 1,
    "order_no": "ORD20260903080000000123",
    "user_id": 1,
    "user_phone": "13800000001",
    "charger_id": 1,
    "charger_code": "SP-F-001",
    "station_name": "Software Park Charging Station",
    "status": "reserved",
    "energy_kwh": 0,
    "amount_cents": 0,
    "amount_yuan": 0
  }
}
```

Validation rules:

- User must exist and be active.
- Station must be open.
- Charger must be idle.
- Charger must not already have a `reserved` or `charging` order.
- A valid user `session_token` is required.

## `order.start`

Request:

```json
{"action":"order.start","session_token":"user-1-1788412345678-abc123","order_id":1}
```

The order changes from `reserved` to `charging`, and the charger changes to `charging`.
The caller must be the order owner or an administrator.

## `order.stop`

Request:

```json
{"action":"order.stop","session_token":"user-1-1788412345678-abc123","order_id":1,"energy_kwh":12.5}
```

The order changes from `charging` to `pending_settlement`, final energy is recorded, telemetry is inserted, and the charger returns to `idle`.
The caller must be the order owner or an administrator.

## `order.cancel`

Request:

```json
{"action":"order.cancel","session_token":"user-1-1788412345678-abc123","order_id":1}
```

Only `reserved` orders can be cancelled. The caller must be the order owner or an administrator.

## `order.settle`

Request:

```json
{"action":"order.settle","session_token":"user-1-1788412345678-abc123","order_id":1}
```

Optional field:

- `energy_kwh`: overrides the stopped energy value when provided.

The demo billing rule is `120` cents per kWh. Settlement is transactional: order status, amount, user balance, balance log and charger statistics are updated together.
The caller must be the order owner or an administrator.

## `order.current`

Request:

```json
{"action":"order.current","session_token":"user-1-1788412345678-abc123"}
```

Returns the latest active order for the logged-in user, if any.
When telemetry exists, the response also includes `latest_telemetry`, `estimated_energy_kwh`, `estimated_amount_cents`, `estimated_amount_yuan` and `price_cents_per_kwh`.

## `order.list`

Request:

```json
{"action":"order.list","session_token":"user-1-1788412345678-abc123"}
```

Optional fields:

- `user_id`: administrators may filter by user; users may only omit it or provide their own id.
- `status`: filters by order status.
- `limit`: defaults to `50`, maximum `200`.

`session_token` is required. Users can only list their own orders. Administrators can
view all orders or filter by `user_id`.

## `balance.recharge`

Request by yuan:

```json
{"action":"balance.recharge","session_token":"user-1-1788412345678-abc123","amount":50}
```

Request by cents:

```json
{"action":"balance.recharge","session_token":"user-1-1788412345678-abc123","amount_cents":5000}
```

Success data includes the new balance and balance log id.

## `balance.logs`

Request:

```json
{"action":"balance.logs","session_token":"user-1-1788412345678-abc123","limit":5}
```

`session_token` is required. Users can only see their own balance logs. Administrators
may add `user_id` to query others.

## `telemetry.report`

Request:

```json
{
  "action": "telemetry.report",
  "charger_id": 1,
  "status": "charging",
  "power_kw": 88.5,
  "energy_kwh": 0.8
}
```

Creates a telemetry record and refreshes charger status and current power.

## `telemetry.list`

Request:

```json
{"action":"telemetry.list","charger_id":1,"limit":5}
```

Returns recent telemetry rows for one charger.

## `statistics.overview`

Request:

```json
{"action":"statistics.overview","session_token":"admin-1-1788412345678-abc123","station_id":1}
```

Administrator-only endpoint. `station_id` is optional; omit it or use `0` for all stations.
Optional `from` and `to` fields filter order records using ISO-8601 timestamps.

The response contains order count, completed energy, revenue, average completed energy,
current charger load, entity counts, plus `order_statuses` and `charger_statuses` arrays.

## `statistics.load_history`

Request:

```json
{"action":"statistics.load_history","session_token":"admin-1-1788412345678-abc123","station_id":1,"limit":50}
```

Administrator-only endpoint. `station_id` is optional and `limit` defaults to `50`
with a maximum of `200`.

The response returns chronological `samples`. Each sample groups telemetry records
with the same `recorded_at` timestamp and contains `actual_load_kw`, the summed
power of the matching station's chargers. An empty `samples` array is a valid
result when no telemetry exists.

## `forecast.generate`

Request:

```json
{
  "action":"forecast.generate",
  "session_token":"admin-1-1788412345678-abc123",
  "station_id":1,
  "horizon_hours":6
}
```

Administrator-only endpoint. `station_id` is optional and `horizon_hours` must be `1`,
`6`, or `24`.

The first implementation uses an explainable demo baseline based on current charger load,
recent telemetry power, and installed charger capacity. It persists every generated result
in `load_forecasts`.

## `forecast.list`

Request:

```json
{"action":"forecast.list","session_token":"admin-1-1788412345678-abc123","limit":20}
```

Administrator-only endpoint. Optional fields:

- `station_id`: filters one station.
- `horizon_hours`: `1`, `6`, or `24`.
- `limit`: defaults to `20`, maximum `200`.

Returns stored forecast records with station name, horizon, predicted load, and generation time.
