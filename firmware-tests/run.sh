#!/usr/bin/env bash
# Builds and runs the firmware's host tests (see README.md). Needs bash and g++ with C++17.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fw="$here/../firmware-mass-spring-damper-sys-id-lab"
out="$here/build"
mkdir -p "$out"

# The decoder test drives the sketch's own processEncoderChange(), copied out of the .ino
awk '/^void processEncoderChange\(\) \{/{p=1} p{print} p&&/^\}/{exit}' \
  "$fw/firmware-mass-spring-damper-sys-id-lab.ino" > "$out/decoder_extracted.inc"
if [ ! -s "$out/decoder_extracted.inc" ]; then
  echo "processEncoderChange() not found in the sketch" >&2
  exit 1
fi

g++ -std=gnu++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I"$here" -I"$fw" \
  -o "$out/calibration_test" "$here/calibration_test.cpp" "$fw/sweep.cpp" "$fw/gpio.cpp" "$fw/calibration.cpp"
g++ -std=gnu++17 -O2 -w -I"$here" -I"$fw" -I"$out" \
  -o "$out/decoder_test" "$here/decoder_test.cpp" "$fw/gpio.cpp"

status=0
echo "Calibration (full log in firmware-tests/build/calibration_test.log):"
"$out/calibration_test" > "$out/calibration_test.log" || status=1
grep -E "FAIL|checks, " "$out/calibration_test.log" || true
echo "Decoder:"
"$out/decoder_test" || status=1
exit $status
