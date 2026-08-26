#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: Verify-QuetooRace.sh --app DIR --architecture ARCH

Required:
  --app DIR            Assembled Quetoo.app.
  --architecture ARCH  arm64, x86_64, or universal.
EOF
}

app=
architecture=

while (($#)); do
  case "$1" in
    --app)
      app=${2:?missing --app value}
      shift 2
      ;;
    --architecture)
      architecture=${2:?missing --architecture value}
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

if [[ -z $app || -z $architecture ]]; then
  usage >&2
  exit 2
fi

case "$architecture" in
  arm64|x86_64|universal) ;;
  *)
    echo "Unsupported macOS architecture: $architecture" >&2
    exit 2
    ;;
esac

if [[ $(uname -s) != Darwin ]]; then
  echo "This verifier requires native macOS tooling." >&2
  exit 1
fi

for command in file lipo nm otool shasum stat; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required tool is unavailable: $command" >&2
    exit 1
  fi
done

if [[ ! -d $app ]]; then
  echo "Quetoo app does not exist: $app" >&2
  exit 1
fi

app=$(cd "$app" && pwd -P)
module_root="$app/Contents/MacOS/lib/quetoo/race"
resource_root="$app/Contents/Resources/race"
host="$app/Contents/MacOS/quetoo"
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
repo_root=$(cd "$script_dir/.." && pwd -P)
payload_verifier="$repo_root/src/tools/verify_race_payload.py"

if [[ ! -f $host || ! -d $module_root || ! -d $resource_root ]]; then
  echo "Race module or resource root is missing from the app." >&2
  exit 1
fi

if find "$module_root" "$resource_root" -type l -print -quit | grep -q .; then
  echo "Race runtime additions must not contain symbolic links." >&2
  exit 1
fi

if ! python3 "$payload_verifier" --root "$app" --layout macos-app; then
  echo "Race app payload does not match the canonical inventory." >&2
  exit 1
fi

verify_architecture() {
  local module=$1
  local archs
  archs=$(lipo -archs "$module")

  case "$architecture" in
    arm64|x86_64)
      if [[ $archs != "$architecture" ]]; then
        echo "Unexpected architecture for $module: $archs" >&2
        exit 1
      fi
      ;;
    universal)
      if [[ " $archs " != *" arm64 "* || " $archs " != *" x86_64 "* ]]; then
        echo "Universal Race module is missing an architecture: $module ($archs)" >&2
        exit 1
      fi
      if [[ $(wc -w <<< "$archs") -ne 2 ]]; then
        echo "Universal Race module has unexpected architectures: $module ($archs)" >&2
        exit 1
      fi
      ;;
  esac
}

deployment_targets() {
  otool -l "$1" | awk '
      $1 == "cmd" && $2 == "LC_BUILD_VERSION" {
        command = "build"
        next
      }
      $1 == "cmd" && $2 == "LC_VERSION_MIN_MACOSX" {
        command = "legacy"
        next
      }
      command == "build" && $1 == "minos" {
        print $2
        command = ""
        next
      }
      command == "legacy" && $1 == "version" {
        print $2
        command = ""
      }
    '
}

host_deployment_targets=()
while IFS= read -r target; do
  host_deployment_targets[${#host_deployment_targets[@]}]=$target
done < <(deployment_targets "$host")

if [[ ${#host_deployment_targets[@]} -eq 0 ]]; then
  echo "Quetoo host has no readable macOS deployment target: $host" >&2
  exit 1
fi

verify_deployment_target() {
  local module=$1
  local targets=()

  while IFS= read -r target; do
    targets[${#targets[@]}]=$target
  done < <(deployment_targets "$module")

  if [[ ${#targets[@]} -ne ${#host_deployment_targets[@]} ]]; then
    echo "Race module has an unexpected deployment-target count: $module (${targets[*]:-none})" >&2
    exit 1
  fi

  local index
  for ((index = 0; index < ${#targets[@]}; index++)); do
    if [[ ${targets[$index]} != "${host_deployment_targets[$index]}" ]]; then
      echo "Race module does not match the Quetoo host deployment target: $module (${targets[*]})" >&2
      exit 1
    fi
  done
}

verify_module() {
  local module=$1
  local entry=$2

  if ! file "$module" | grep -q 'Mach-O'; then
    echo "Race module is not Mach-O: $module" >&2
    exit 1
  fi

  verify_architecture "$module"
  verify_deployment_target "$module"

  local symbols
  symbols=$(nm -gU "$module" | awk '{ print $NF }')
  if [[ $(grep -Fxc "_$entry" <<< "$symbols") -ne 1 ]]; then
    echo "Race module does not export exactly one $entry entry point: $module" >&2
    exit 1
  fi

  while read -r dependency _; do
    [[ -n ${dependency:-} ]] || continue
    case "$dependency" in
      @executable_path/*|@loader_path/*|@rpath/*|/usr/lib/*|/System/Library/*) ;;
      *)
        echo "Race module retains a non-bundled dependency: $module -> $dependency" >&2
        exit 1
        ;;
    esac
  done < <(otool -L "$module" | tail -n +2)
}

game="$module_root/game.so"
cgame="$module_root/cgame.so"
verify_module "$game" G_LoadGame
verify_module "$cgame" Cg_LoadCgame

game_size=$(stat -f '%z' "$game")
cgame_size=$(stat -f '%z' "$cgame")
game_sha256=$(shasum -a 256 "$game" | awk '{ print $1 }')
cgame_sha256=$(shasum -a 256 "$cgame" | awk '{ print $1 }')

echo "RACE_APP_VERIFY_PASS files=61 architecture=$architecture deployment_targets=${host_deployment_targets[*]}"
echo "GAME_SIZE=$game_size"
echo "GAME_SHA256=$game_sha256"
echo "CGAME_SIZE=$cgame_size"
echo "CGAME_SHA256=$cgame_sha256"
