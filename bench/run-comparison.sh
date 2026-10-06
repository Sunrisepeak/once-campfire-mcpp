#!/usr/bin/env bash
# Same-machine A/B: once-campfire-mcpp vs once-campfire-rust, driven by the
# official once-campfire-rust load generator through identical route paths,
# concurrency and durations. This removes the machine variable from the
# published table comparison; the method mirrors bench/run in the Rust repo
# (servers pinned to SERVER_CPUS, loadgen pinned to LOADGEN_CPUS, warmup then
# HTTP_SECS of measurement per route).
#
#   bench/run-comparison.sh [cpp|rust|both]
#
# Prerequisites (built by the respective toolchains, paths overridable via env):
#   MCPP_BIN    target/x86_64-linux-gnu/*/bin/once-campfire-mcpp/campfire   (mcpp build)
#   RUST_BIN    ../once-campfire-rust/target/release/campfire               (cargo build --release -p campfire)
#   LOADGEN     ../once-campfire-rust/target/bench/release/loadgen          (bench/loadgen workspace)
#   RUST_SEED   a seeded storage dir: db/production.sqlite3 + files/        (parity/bin/seed build default)
#
# The Rust app runs its production shape: front server on $PORT, app listener
# on $PORT+1, DISABLE_SSL. The mcpp app serves both roles on $PORT. The Rust
# run needs a real login (seed credentials); the mcpp M1 app accepts any
# cookie, so the same loadgen commands run against both unchanged.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
RUST=${RUST:-$ROOT/../once-campfire-rust}
MCPP_BIN=${MCPP_BIN:-$(find "$ROOT"/target -path '*bin/once-campfire-mcpp/campfire' -type f 2>/dev/null | head -1)}
RUST_BIN=${RUST_BIN:-$RUST/target/release/campfire}
LOADGEN=${LOADGEN:-$RUST/target/bench/release/loadgen}
RUST_SEED=${RUST_SEED:-$RUST/parity/.seed/default}

SERVER_CPUS=${SERVER_CPUS:-8-11}
LOADGEN_CPUS=${LOADGEN_CPUS:-12-15}
PORT=${PORT:-4390}
HTTP_SECS=${HTTP_SECS:-8}
CONC=${CONC:-16}
WARMUP_SECS=${WARMUP_SECS:-2}
LABELS=${LABELS:-$RUST_SEED/labels.json}
OUT=${OUT:-$ROOT/bench/results/comparison-$(date +%Y%m%d-%H%M%S)}
mkdir -p "$OUT"

log() { echo "[$(date +%T)] $*" >&2; }
label() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$1" "$2"; }

ROOM=$(label "$LABELS" rooms.watercooler)
WRITE_ROOM=$(label "$LABELS" rooms.hq)
BEFORE=$(label "$LABELS" messages.busy_060)
EMAIL=$(label "$LABELS" emails.david)
PASSWORD=$(label "$LABELS" passwords.all)

lg() { taskset -c "$LOADGEN_CPUS" "$LOADGEN" "$@"; }

start_cpp() {
  taskset -c "$SERVER_CPUS" "$MCPP_BIN" --port "$PORT" --threads 4 \
    >"$OUT/cpp-server.log" 2>&1 & echo $!
}

start_rust() {
  local work="$OUT/.rust-storage"
  rm -rf "$work"; mkdir -p "$work"
  cp -a "$RUST_SEED/db" "$work/db"
  cp -a "$RUST_SEED/storage" "$work/files" 2>/dev/null || mkdir -p "$work/files"
  taskset -c "$SERVER_CPUS" env SECRET_KEY_BASE_DUMMY=1 DISABLE_SSL=1 \
    RAILS_ENV=production HTTP_PORT=$PORT TARGET_PORT=$((PORT + 1)) \
    RAILS_MAX_THREADS=${RAILS_MAX_THREADS:-5} RAILS_LOG_LEVEL=warn \
    CAMPFIRE_STORAGE_PATH="$work" \
    "$RUST_BIN" server >"$OUT/rust-server.log" 2>&1 & echo $!
}

wait_up() {
  for _ in $(seq 1 600); do
    curl -fsS -o /dev/null "http://127.0.0.1:$PORT/up" 2>/dev/null && return 0
    sleep 0.5
  done
  return 1
}

measure() { # measure <app> <name> <path-or-POST>
  local app=$1 name=$2 spec=$3
  local args=()
  if [ "$spec" = POST ]; then
    args=(--post-room "$WRITE_ROOM" --csrf "$CSRF")
  else
    args=(--path "$spec")
  fi
  lg http --base "http://127.0.0.1:$PORT" --cookie "$COOKIE" "${args[@]}" \
    --conc "$CONC" --duration "$WARMUP_SECS" >/dev/null
  local res
  res=$(lg http --base "http://127.0.0.1:$PORT" --cookie "$COOKIE" "${args[@]}" \
    --conc "$CONC" --duration "$HTTP_SECS")
  echo "$res" | python3 -c 'import json,sys; r=json.load(sys.stdin); print(r["rps"], r["latency"].get("p50_ms"), r["latency"].get("p99_ms"), r.get("statuses"), "errors", r.get("errors"))' \
    | xargs printf "%s %-14s rps=%s p50=%sms p99=%sms %s %s\n" "$app" "$name"
  echo "$res" > "$OUT/$app-$name.json"
}

run_app() {
  local app=$1
  local pid; pid=$("start_$app")
  log "$app: starting (pid $pid, cpus $SERVER_CPUS)"
  wait_up || { log "$app: did not come up"; kill "$pid" 2>/dev/null || true; return 1; }
  log "$app: up"

  if [ "$app" = rust ]; then
    COOKIE=$(lg login --base "http://127.0.0.1:$PORT" --email "$EMAIL" --password "$PASSWORD" \
      | python3 -c 'import json,sys; print(json.load(sys.stdin)["cookie"])')
    local scrape
    scrape=$(lg scrape --base "http://127.0.0.1:$PORT" --cookie "$COOKIE" --room "$ROOM")
    CSRF=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["csrf"] or "")' "$scrape")
  else
    COOKIE="bench-mode-no-auth"
    CSRF=""
  fi

  measure "$app" room_show      "/rooms/$ROOM"
  measure "$app" messages_page  "/rooms/$ROOM/messages?before=$BEFORE"
  measure "$app" sidebar        "/users/me/sidebar"
  measure "$app" search         "/searches?q=coffee"
  measure "$app" post_message   POST

  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  sleep 2
}

{
  echo "date: $(date -Is)"
  echo "host: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs), $(nproc) threads"
  echo "server cpus: $SERVER_CPUS; loadgen cpus: $LOADGEN_CPUS; conc: $CONC; http secs: $HTTP_SECS"
  echo "mcpp bin: $MCPP_BIN"
  echo "rust bin: $RUST_BIN ($(git -C "$RUST" rev-parse --short HEAD 2>/dev/null || echo tarball))"
} > "$OUT/env.txt"

case "${1:-both}" in
  cpp)  run_app cpp ;;
  rust) run_app rust ;;
  both) run_app cpp; run_app rust ;;
  *) echo "usage: $0 [cpp|rust|both]" >&2; exit 2 ;;
esac

log "results in $OUT"
