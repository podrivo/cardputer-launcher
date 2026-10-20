#!/usr/bin/env bash
# Build a tiny demo app-only image into bins/demo.bin for local packing tests.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p bins

if [[ ! -d components/M5GFX ]]; then
  git clone --branch 0.2.33 --depth 1 https://github.com/m5stack/M5GFX.git components/M5GFX
fi

export IDF_CCACHE_ENABLE=1
export ESPBAUD="${ESPBAUD:-921600}"
IDF_ROOT="${IDF_PATH:-$HOME/esp/esp-idf}"

idf_ready() {
  [[ -n "${IDF_PYTHON_ENV_PATH:-}" ]] && command -v idf.py >/dev/null 2>&1
}
if ! idf_ready; then
  # shellcheck disable=SC1091
  . "$IDF_ROOT/export.sh"
fi

# Demo is a second IDF project under demo/ that reuses ../components/M5GFX.
pushd demo >/dev/null
idf.py set-target esp32s3 >/dev/null
idf.py build
APP="$(pwd)/build/cardputer_launcher_demo.bin"
if [[ ! -f "$APP" ]]; then
  # IDF names the app bin after the project.
  APP="$(ls build/*.bin | head -1)"
fi
cp -f "$APP" "$ROOT/bins/demo.bin"
popd >/dev/null
echo "Wrote $ROOT/bins/demo.bin ($(wc -c < "$ROOT/bins/demo.bin") bytes)"
