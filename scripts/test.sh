#!/bin/sh
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
python3 -m unittest discover -s tests/synology -p 'test_*.py'
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 tests/synology/test_logic.cpp -o build/tests/test_logic
build/tests/test_logic
"${CXX:-c++}" -std=c++11 tests/trains/test_logic.cpp -o build/tests/test_trains
"${CXX:-c++}" -std=c++11 tests/trains/test_schedule.cpp -o build/tests/test_schedule
build/tests/test_schedule
build/tests/test_trains
if [ -n "${ARDUINOJSON_INCLUDE:-}" ]; then
  "${CXX:-c++}" -std=c++11 -I"$ARDUINOJSON_INCLUDE" tests/trains/test_board.cpp -o build/tests/test_rail_board
  build/tests/test_rail_board
  "${CXX:-c++}" -std=c++11 -I"$ARDUINOJSON_INCLUDE" tests/trains/test_rtt.cpp -o build/tests/test_rtt
  build/tests/test_rtt
  "${CXX:-c++}" -std=c++11 -I"$ARDUINOJSON_INCLUDE" tests/trains/test_schedule_json.cpp -o build/tests/test_schedule_json
  build/tests/test_schedule_json
fi
python3 scripts/check_public_tree.py
echo 'Sender, native firmware logic and public-tree checks passed.'
