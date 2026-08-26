#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: Smoke-QuetooRace.sh --app DIR --data-root DIR --home DIR --output DIR
                           [--portability-fixture DIR]

Runs native dedicated and client smokes against a matching-source Quetoo app.
DIR is the matching quetoo-data target root containing default/. HOME is an
isolated per-user root used only for runtime writes. When supplied, the
portability fixture is loaded through the production persistence,
QRPL, playback, and raceline paths.
EOF
}

app=
data_root=
smoke_home=
output=
portability_fixture=

while (($#)); do
  case "$1" in
    --app)
      app=${2:?missing --app value}
      shift 2
      ;;
    --data-root)
      data_root=${2:?missing --data-root value}
      shift 2
      ;;
    --home)
      smoke_home=${2:?missing --home value}
      shift 2
      ;;
    --output)
      output=${2:?missing --output value}
      shift 2
      ;;
    --portability-fixture)
      portability_fixture=${2:?missing --portability-fixture value}
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z $app || -z $data_root || -z $smoke_home || -z $output ]]; then
  usage >&2
  exit 2
fi

if [[ $(uname -s) != Darwin ]]; then
  echo "This smoke requires native macOS." >&2
  exit 1
fi

for command in cmp grep python3 shasum stat; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required tool is unavailable: $command" >&2
    exit 1
  fi
done

app=$(cd "$app" && pwd -P)
data_root=$(cd "$data_root" && pwd -P)
mkdir -p "$smoke_home" "$output"
smoke_home=$(cd "$smoke_home" && pwd -P)
output=$(cd "$output" && pwd -P)
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
if [[ -n $portability_fixture ]]; then
  portability_fixture=$(cd "$portability_fixture" && pwd -P)
fi

dedicated="$app/Contents/MacOS/quetoo-dedicated"
client="$app/Contents/MacOS/quetoo"
module_root="$app/Contents/MacOS/lib/quetoo/race"
resource_root="$app/Contents/Resources/race"
server_home="$output/server-home"
client_home="$output/client-home"
server_user_dir="$server_home/Library/Application Support/WickedOldGames/Quetoo"
client_user_dir="$client_home/Library/Application Support/WickedOldGames/Quetoo"
server_race_dir="$server_user_dir/race"
client_race_dir="$client_user_dir/race"
server_write_dir="$output/server-write"
client_write_dir="$output/client-write"
port=${RACE_SMOKE_PORT:-27980}
runtime_map=potato
reload_marker='Server initialized'
client_name=race-smoke
client_active_marker='race-smoke wants some'
client_connect_marker='race-smoke connected'
client_disconnect_marker='race-smoke disconnected'
client_guid=
physics_args=()
release_args=(
  --release-command "stuff $client_name r_screenshot view"
  --release-command "stuff $client_name say MACOS_RACE_VISUAL_SYNC"
  --release-marker 'MACOS_RACE_VISUAL_SYNC'
  --post-release-command "stuff $client_name quit"
  --post-release-marker "$client_disconnect_marker"
)
if [[ -n $portability_fixture ]]; then
  runtime_map=edge
  reload_marker='Race course: map=edge'
  client_name=Runner
  client_active_marker='Runner wants some'
  client_connect_marker='Runner connected'
  client_disconnect_marker='Runner disconnected'
  client_guid='01234567-89ab-4cde-8f01-23456789abcd'
  physics_args=(+set g_race_physics quetoo-common-v1)
  control_waits=
  for ((wait_index = 0; wait_index < 8; wait_index++)); do
    control_waits+='; wait'
  done
  playback_batch="race replay wr; race replay pause${control_waits}; race replay step_forward${control_waits}; race replay speed 2${control_waits}; race replay resume; say MACOS_RACE_PLAYBACK_SYNC"
  persistence_batch="raceline wr${control_waits}; r_screenshot view${control_waits}; say MACOS_RACE_PERSISTENCE_SYNC${control_waits}; raceline off${control_waits}; quit"
  for command_batch in "$playback_batch" "$persistence_batch"; do
    if ((${#command_batch} >= 512)); then
      echo "Portability smoke command batch exceeds the host command buffer." >&2
      exit 1
    fi
  done
  release_args=(
    --release-command "stuff $client_name \"$playback_batch\""
    --release-marker 'MACOS_RACE_PLAYBACK_SYNC'
    --post-release-command "stuff $client_name \"$persistence_batch\""
    --post-release-marker "$client_disconnect_marker"
  )
fi
if [[ ! $port =~ ^[0-9]+$ || $port -lt 1024 || $port -gt 65535 ]]; then
  echo "Invalid RACE_SMOKE_PORT: $port" >&2
  exit 2
fi
mkdir -p \
  "$server_race_dir" "$client_race_dir" \
  "$server_write_dir" "$client_write_dir"


if [[ -n $portability_fixture ]]; then
  for name in profile.profile map.state gset.cfg replay.qrpl wire.bin; do
    if [[ ! -f $portability_fixture/$name ]]; then
      echo "Portability fixture input is missing: $portability_fixture/$name" >&2
      exit 1
    fi
  done
  mkdir -p \
    "$server_write_dir/profiles" \
    "$server_write_dir/state/quetoo-common-v1" \
    "$server_write_dir/replays/quetoo-common-v1/65646765" \
    "$server_write_dir/race"
  cp "$portability_fixture/profile.profile" \
    "$server_write_dir/profiles/$client_guid.profile"
  cp "$portability_fixture/map.state" \
    "$server_write_dir/state/quetoo-common-v1/65646765.state"
  cp "$portability_fixture/replay.qrpl" \
    "$server_write_dir/replays/quetoo-common-v1/65646765/replay-dba8d53d1dc24cab.ghost"
  cp "$portability_fixture/gset.cfg" \
    "$server_write_dir/race/gset.cfg"
  cp "$portability_fixture/wire.bin" "$output/portability-wire.bin"
fi

runtime_map_path="$resource_root/maps/potato.bsp"
if [[ -n $portability_fixture ]]; then
  runtime_map_path="$data_root/default/maps/edge.bsp"
fi

for required in \
    "$dedicated" \
    "$client" \
    "$module_root/game.so" \
    "$module_root/cgame.so" \
    "$runtime_map_path" \
    "$data_root/default/players/qforcer/default.skin"; do
  if [[ ! -e $required ]]; then
    echo "Runtime smoke input is missing: $required" >&2
    exit 1
  fi
done

dedicated_stdout="$output/dedicated.stdout.log"
server_ready="$output/dedicated.ready"
server_release="$output/dedicated.release"
rm -f "$server_ready" "$server_release"
server_driver_pid=
client_pid=
cleanup_server() {
  if [[ -n ${client_pid:-} ]] && kill -0 "$client_pid" 2>/dev/null; then
    kill "$client_pid" 2>/dev/null || true
    wait "$client_pid" 2>/dev/null || true
  fi
  if [[ -n ${server_driver_pid:-} ]] && kill -0 "$server_driver_pid" 2>/dev/null; then
    kill "$server_driver_pid" 2>/dev/null || true
    wait "$server_driver_pid" 2>/dev/null || true
  fi
}
trap cleanup_server EXIT

HOME="$server_home" \
  python3 "$script_dir/../src/tools/run_pty_commands.py" \
    --log "$dedicated_stdout" \
    --timeout 60 \
    --ready-marker 'Server initialized' \
    --reload-command "map $runtime_map" \
    --reload-marker "$reload_marker" \
    --ready-file "$server_ready" \
    --release-file "$server_release" \
    --release-timeout 180 \
    "${release_args[@]}" \
    --quit-command quit \
    -- "$dedicated" --wpath "$server_write_dir" -p "$data_root" \
      +set version -1 +set sv_public 0 +set net_port "$port" \
      "${physics_args[@]}" +game race +map potato &
server_driver_pid=$!

for ((i = 0; i < 240; i++)); do
  if [[ -f $server_ready ]]; then
    break
  fi
  if ! kill -0 "$server_driver_pid" 2>/dev/null; then
    wait "$server_driver_pid" || true
    echo "Dedicated smoke exited before the external server was ready." >&2
    exit 1
  fi
  sleep 0.25
done
if [[ ! -f $server_ready ]]; then
  echo "Dedicated smoke did not become ready for an external client." >&2
  exit 1
fi

dedicated_log="$server_user_dir/default/quetoo.log"
if [[ ! -f $dedicated_log ]]; then
  echo "Dedicated smoke did not produce the runtime log." >&2
  exit 1
fi
cp "$dedicated_log" "$output/dedicated.quetoo.log"

for marker in '/race/game.so' "Race course: map=$runtime_map" 'Server initialized'; do
  if ! grep -Fq "$marker" "$dedicated_stdout" "$dedicated_log"; then
    echo "Dedicated smoke is missing marker: $marker" >&2
    exit 1
  fi
done
server_image="G_LoadGame from $module_root/game.so"
if ! grep -Fq "$server_image" "$dedicated_stdout" "$dedicated_log"; then
  echo "Dedicated smoke did not resolve the bundled GAME image: $server_image" >&2
  exit 1
fi

client_cfg="$client_race_dir/mac-client-smoke.cfg"
{
  printf 'cl_timeout 60\n'
  printf 'cl_max_fps 60\n'
  if [[ -n $client_guid ]]; then
    printf 'set guid "%s"\n' "$client_guid"
  fi
  printf 'name "%s"\n' "$client_name"
  printf 'connect 127.0.0.1:%s\n' "$port"
} > "$client_cfg"

client_stdout="$output/client.stdout.log"
: > "$client_stdout"
HOME="$client_home" ALSOFT_DRIVERS=null "$client" \
  --wpath "$client_write_dir" -p "$data_root" \
  +set version -1 +set r_fullscreen 0 \
  +set r_window_width 1280 +set r_window_height 720 \
  +game race +exec mac-client-smoke.cfg \
  > "$client_stdout" 2>&1 &
client_pid=$!

client_log="$client_user_dir/default/quetoo.log"
for ((i = 0; i < 720; i++)); do
  if grep -Fq "$client_active_marker" "$dedicated_log" "$client_stdout"; then
    break
  fi
  if ! kill -0 "$client_pid" 2>/dev/null; then
    wait "$client_pid" || true
    echo "Client smoke exited before entering active Race play." >&2
    exit 1
  fi
  sleep 0.25
done
if ! grep -Fq "$client_active_marker" "$dedicated_log" "$client_stdout"; then
  echo "Client smoke did not enter active Race play." >&2
  exit 1
fi

# The client intentionally rate-limits its fallback tuning sync for 1.5 seconds.
# Let the authoritative stream settle before capturing runtime evidence.
sleep 2
touch "$server_release"
client_deadline=$((SECONDS + 180))
while kill -0 "$client_pid" 2>/dev/null; do
  if ((SECONDS >= client_deadline)); then
    kill "$client_pid" 2>/dev/null || true
    wait "$client_pid" 2>/dev/null || true
    echo "Client smoke did not exit within 180 seconds after release." >&2
    exit 1
  fi
  sleep 0.25
done
if ! wait "$client_pid"; then
  echo "Client smoke exited unsuccessfully." >&2
  exit 1
fi
client_pid=
if ! wait "$server_driver_pid"; then
  echo "Dedicated smoke exited unsuccessfully after the client run." >&2
  exit 1
fi
server_driver_pid=
cp "$dedicated_log" "$output/dedicated.quetoo.log"
for marker in "$client_connect_marker" "$client_disconnect_marker"; do
  if ! grep -Fq "$marker" "$dedicated_stdout" "$dedicated_log"; then
    echo "Dedicated smoke is missing external-client marker: $marker" >&2
    exit 1
  fi
done

if [[ ! -f $client_log ]]; then
  echo "Client smoke did not produce the runtime log." >&2
  exit 1
fi
cp "$client_log" "$output/client.quetoo.log"

client_map_marker='Potato jumps'
client_sync_marker='MACOS_RACE_VISUAL_SYNC'
if [[ -n $portability_fixture ]]; then
  client_map_marker='The Edge'
  client_sync_marker='MACOS_RACE_PERSISTENCE_SYNC'
fi
for marker in '/race/cgame.so' "Connecting to 127.0.0.1:$port" \
               'You joined Race Mode.' \
               "$client_map_marker" \
               "$client_sync_marker"; do
  if ! grep -Fq "$marker" "$client_stdout" "$client_log"; then
    echo "Client smoke is missing marker: $marker" >&2
    exit 1
  fi
done
for image in \
    "G_LoadGame from $module_root/game.so" \
    "Cg_LoadCgame from $module_root/cgame.so"; do
  if ! grep -Fq "$image" "$client_stdout" "$client_log"; then
    echo "Client smoke did not resolve the bundled module image: $image" >&2
    exit 1
  fi
done

if [[ -n $portability_fixture ]]; then
  for marker in \
      'Race global cvars: source=committed' \
      'Race map state: status=validating-replays source=committed map=edge ruleset=quetoo-common-v1 generation=42 records=1 publication=replay-backed' \
      'Race map state: replay validation complete map=edge records=1' \
      'Replaying Runner - 0:00.050' \
      'Replay paused.' \
      'Replay stepped forward one sample and paused.' \
      'Replay speed changed.' \
      'Replay resumed.' \
      'Raceline: Runner - 0:00.050' \
      'Raceline hidden.'; do
    if ! grep -Fq "$marker" "$dedicated_stdout" "$dedicated_log" \
                                  "$client_stdout" "$client_log"; then
      echo "Persistence runtime smoke is missing marker: $marker" >&2
      exit 1
    fi
  done
  if grep -Fq 'Replay controls are cooling down.' \
       "$client_stdout" "$client_log" || \
     grep -Fq 'CMD_MAX_STRINGS exceeded' \
       "$dedicated_stdout" "$dedicated_log"; then
    echo "Persistence runtime smoke violated a stock command boundary." >&2
    exit 1
  fi
  cmp "$portability_fixture/profile.profile" \
    "$server_write_dir/profiles/$client_guid.profile"
  cmp "$portability_fixture/map.state" \
    "$server_write_dir/state/quetoo-common-v1/65646765.state"
  cmp "$portability_fixture/replay.qrpl" \
    "$server_write_dir/replays/quetoo-common-v1/65646765/replay-dba8d53d1dc24cab.ghost"
  cmp "$portability_fixture/gset.cfg" \
    "$server_write_dir/race/gset.cfg"
  cmp "$portability_fixture/wire.bin" "$output/portability-wire.bin"
fi

screenshot=$(find "$client_write_dir/screenshots" -type f -name '*.jpg' -print -quit 2>/dev/null || true)
if [[ -z $screenshot ]]; then
  echo "Client smoke did not capture a rendered Race frame." >&2
  exit 1
fi

screenshot_size=$(stat -f '%z' "$screenshot")
screenshot_sha256=$(shasum -a 256 "$screenshot" | awk '{ print $1 }')
dedicated_sha256=$(shasum -a 256 "$output/dedicated.quetoo.log" | awk '{ print $1 }')
client_sha256=$(shasum -a 256 "$output/client.quetoo.log" | awk '{ print $1 }')
runtime_evidence="$output/runtime-evidence.meta"
{
  printf 'format=1\n'
  printf 'platform=macos\n'
  printf 'architecture=%s\n' "$(uname -m)"
  printf 'external_client_server=1\n'
  if [[ -n $portability_fixture ]]; then
    printf 'portability_runtime=1\n'
    printf 'portability_replay_id=dba8d53d1dc24cab\n'
    for entry in \
        "profile:$server_write_dir/profiles/$client_guid.profile" \
        "map_state:$server_write_dir/state/quetoo-common-v1/65646765.state" \
        "settings:$server_write_dir/race/gset.cfg" \
        "replay:$server_write_dir/replays/quetoo-common-v1/65646765/replay-dba8d53d1dc24cab.ghost" \
        "wire:$output/portability-wire.bin"; do
      name=${entry%%:*}
      path=${entry#*:}
      printf 'portability_%s_size=%s\n' "$name" "$(stat -f '%z' "$path")"
      printf 'portability_%s_sha256=%s\n' "$name" \
        "$(shasum -a 256 "$path" | awk '{ print $1 }')"
    done
  else
    printf 'portability_runtime=0\n'
  fi
  printf 'client_size=%s\n' "$(stat -f '%z' "$client")"
  printf 'client_sha256=%s\n' "$(shasum -a 256 "$client" | awk '{ print $1 }')"
  printf 'dedicated_size=%s\n' "$(stat -f '%z' "$dedicated")"
  printf 'dedicated_sha256=%s\n' "$(shasum -a 256 "$dedicated" | awk '{ print $1 }')"
  printf 'game_size=%s\n' "$(stat -f '%z' "$module_root/game.so")"
  printf 'game_sha256=%s\n' "$(shasum -a 256 "$module_root/game.so" | awk '{ print $1 }')"
  printf 'cgame_size=%s\n' "$(stat -f '%z' "$module_root/cgame.so")"
  printf 'cgame_sha256=%s\n' "$(shasum -a 256 "$module_root/cgame.so" | awk '{ print $1 }')"
  printf 'dedicated_log_size=%s\n' "$(stat -f '%z' "$output/dedicated.quetoo.log")"
  printf 'dedicated_log_sha256=%s\n' "$dedicated_sha256"
  printf 'client_log_size=%s\n' "$(stat -f '%z' "$output/client.quetoo.log")"
  printf 'client_log_sha256=%s\n' "$client_sha256"
  printf 'screenshot_size=%s\n' "$screenshot_size"
  printf 'screenshot_sha256=%s\n' "$screenshot_sha256"
} > "$runtime_evidence"

echo "RACE_MACOS_RUNTIME_PASS dedicated=1 client=1 external_client_server=1 portability_runtime=$([[ -n $portability_fixture ]] && echo 1 || echo 0) screenshot=1"
echo "DEDICATED_LOG_SHA256=$dedicated_sha256"
echo "CLIENT_LOG_SHA256=$client_sha256"
echo "SCREENSHOT=$screenshot"
echo "SCREENSHOT_SIZE=$screenshot_size"
echo "SCREENSHOT_SHA256=$screenshot_sha256"
echo "EVIDENCE=$runtime_evidence"
