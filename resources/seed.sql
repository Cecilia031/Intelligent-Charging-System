INSERT OR IGNORE INTO admins (id, username, password_hash, password_salt, display_name, status)
VALUES
    (1, 'admin', '2c1d4540d2225c9534c85caeb2e73910af4dedc56af9f7992c4e9d943bfe23f7', 'charging-demo-salt', 'System Administrator', 'active'),
    (2, 'disabled_admin', '2c1d4540d2225c9534c85caeb2e73910af4dedc56af9f7992c4e9d943bfe23f7', 'charging-demo-salt', 'Disabled Administrator', 'disabled');

INSERT OR IGNORE INTO users (id, phone, nickname, avatar_url, balance_cents, status)
VALUES
    (1, '13800000001', 'Demo User', '', 12000, 'active'),
    (2, '13800000002', 'Frozen User', '', 5000, 'frozen'),
    (3, '13800000003', 'Low Balance User', '', 600, 'active');

INSERT OR IGNORE INTO stations (id, name, address, latitude, longitude, status)
VALUES
    (1, 'Software Park Charging Station', 'No. 1 Software Park Road', 39.983800, 116.315900, 'open'),
    (2, 'Campus East Gate Station', 'East Gate Parking Lot', 39.981500, 116.320100, 'open'),
    (3, 'Maintenance Station', 'Backup Service Area', 39.979000, 116.318300, 'closed');

INSERT OR IGNORE INTO chargers (id, station_id, code, type, power_kw, status, current_power_kw)
VALUES
    (1, 1, 'SP-F-001', 'fast', 120.0, 'idle', 0),
    (2, 1, 'SP-F-002', 'fast', 120.0, 'charging', 72.5),
    (3, 1, 'SP-S-001', 'slow', 7.0, 'idle', 0),
    (4, 2, 'CE-F-001', 'fast', 90.0, 'idle', 0),
    (5, 2, 'CE-S-001', 'slow', 7.0, 'fault', 0),
    (6, 3, 'MS-F-001', 'fast', 60.0, 'offline', 0);

INSERT OR IGNORE INTO charger_telemetry (id, charger_id, status, power_kw, energy_kwh)
VALUES
    (1, 2, 'charging', 72.5, 18.2),
    (2, 1, 'idle', 0.0, 0.0);
