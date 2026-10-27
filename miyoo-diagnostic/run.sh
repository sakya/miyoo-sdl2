#!/bin/sh
cd "$(dirname "$0")" || exit 1
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH:-}"
./miyoo_sdl2_diagnostic miyoo-results.csv > miyoo-stdout.log 2>&1
status=$?
echo "exit_status=$status" >> miyoo-stdout.log
exit "$status"
