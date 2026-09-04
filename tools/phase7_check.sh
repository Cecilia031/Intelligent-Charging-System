#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-45454}"
TEST_DB="${TEST_DB:-$ROOT_DIR/data/phase7_check.sqlite}"
SERVER_PID=""

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}

wait_for_server() {
    for _ in $(seq 1 30); do
        if python3 "$ROOT_DIR/tools/send_json.py" --port "$PORT" '{"action":"ping"}' >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    echo "server did not become ready on port $PORT" >&2
    return 1
}

build_qmake_target() {
    local build_dir="$1"
    local pro_file="$2"

    mkdir -p "$ROOT_DIR/$build_dir"
    (
        cd "$ROOT_DIR/$build_dir"
        qmake "../$pro_file"
        make -j"$(nproc)"
    )
}

trap cleanup EXIT

echo "[1/7] Build server"
build_qmake_target build-qmake charging_server.pro

echo "[2/7] Build user client"
build_qmake_target build-user-qmake user_client.pro

echo "[3/7] Build admin client"
build_qmake_target build-admin-qmake admin_client.pro

echo "[4/7] Initialize temporary database"
mkdir -p "$(dirname "$TEST_DB")"
rm -f "$TEST_DB"
"$ROOT_DIR/build-qmake/charging_server" \
    --init-only \
    --db "$TEST_DB" \
    --schema "$ROOT_DIR/resources/schema.sql" \
    --seed "$ROOT_DIR/resources/seed.sql"

echo "[5/7] Start server on port $PORT"
"$ROOT_DIR/build-qmake/charging_server" \
    --port "$PORT" \
    --db "$TEST_DB" \
    --schema "$ROOT_DIR/resources/schema.sql" \
    --seed "$ROOT_DIR/resources/seed.sql" &
SERVER_PID="$!"
wait_for_server

echo "[6/7] Run protocol checks"
python3 "$ROOT_DIR/tools/smoke_test.py" --port "$PORT"
python3 "$ROOT_DIR/tools/invalid_request_test.py" --port "$PORT"
python3 "$ROOT_DIR/tools/network_resilience_test.py" --port "$PORT"
python3 "$ROOT_DIR/tools/concurrency_test.py" --port "$PORT" --clients 20
python3 "$ROOT_DIR/tools/regression_test.py" --port "$PORT"

echo "[7/7] Phase 7 checks passed"
