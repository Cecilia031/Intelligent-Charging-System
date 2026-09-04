PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS admins (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    password_salt TEXT NOT NULL,
    display_name TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'disabled')),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    phone TEXT NOT NULL UNIQUE,
    nickname TEXT NOT NULL,
    avatar_url TEXT NOT NULL DEFAULT '',
    balance_cents INTEGER NOT NULL DEFAULT 0 CHECK (balance_cents >= 0),
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'frozen')),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS auth_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    actor_type TEXT NOT NULL CHECK (actor_type IN ('user', 'admin')),
    actor_id INTEGER NOT NULL,
    token_hash TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_auth_sessions_actor ON auth_sessions(actor_type, actor_id);

CREATE TABLE IF NOT EXISTS stations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    address TEXT NOT NULL,
    latitude REAL NOT NULL,
    longitude REAL NOT NULL,
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'closed')),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS chargers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER NOT NULL REFERENCES stations(id) ON DELETE RESTRICT,
    code TEXT NOT NULL UNIQUE,
    type TEXT NOT NULL CHECK (type IN ('fast', 'slow')),
    power_kw REAL NOT NULL CHECK (power_kw > 0),
    status TEXT NOT NULL DEFAULT 'idle' CHECK (status IN ('idle', 'charging', 'fault', 'offline')),
    current_power_kw REAL NOT NULL DEFAULT 0 CHECK (current_power_kw >= 0),
    total_orders INTEGER NOT NULL DEFAULT 0 CHECK (total_orders >= 0),
    total_energy_kwh REAL NOT NULL DEFAULT 0 CHECK (total_energy_kwh >= 0),
    total_duration_minutes INTEGER NOT NULL DEFAULT 0 CHECK (total_duration_minutes >= 0),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS charging_orders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    order_no TEXT NOT NULL UNIQUE,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    charger_id INTEGER NOT NULL REFERENCES chargers(id) ON DELETE RESTRICT,
    status TEXT NOT NULL CHECK (status IN ('reserved', 'charging', 'pending_settlement', 'completed', 'cancelled')),
    reserved_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at TEXT,
    stopped_at TEXT,
    settled_at TEXT,
    energy_kwh REAL NOT NULL DEFAULT 0 CHECK (energy_kwh >= 0),
    amount_cents INTEGER NOT NULL DEFAULT 0 CHECK (amount_cents >= 0),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_orders_user_status ON charging_orders(user_id, status);
CREATE INDEX IF NOT EXISTS idx_orders_charger_status ON charging_orders(charger_id, status);

CREATE TABLE IF NOT EXISTS balance_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    change_cents INTEGER NOT NULL,
    balance_after_cents INTEGER NOT NULL CHECK (balance_after_cents >= 0),
    reason TEXT NOT NULL,
    related_order_id INTEGER REFERENCES charging_orders(id) ON DELETE SET NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS charger_telemetry (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    charger_id INTEGER NOT NULL REFERENCES chargers(id) ON DELETE CASCADE,
    status TEXT NOT NULL CHECK (status IN ('idle', 'charging', 'fault', 'offline')),
    power_kw REAL NOT NULL CHECK (power_kw >= 0),
    energy_kwh REAL NOT NULL CHECK (energy_kwh >= 0),
    recorded_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_telemetry_charger_time ON charger_telemetry(charger_id, recorded_at);

CREATE TABLE IF NOT EXISTS load_forecasts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER REFERENCES stations(id) ON DELETE CASCADE,
    horizon_hours INTEGER NOT NULL CHECK (horizon_hours IN (1, 6, 24)),
    predicted_load_kw REAL NOT NULL CHECK (predicted_load_kw >= 0),
    generated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
