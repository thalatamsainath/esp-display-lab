#!/bin/sh
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
python3 -m unittest discover -s tests/synology -p 'test_*.py'
python3 -m unittest discover -s tests/limits -p 'test_*.py'
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 tests/synology/test_logic.cpp -o build/tests/test_logic
build/tests/test_logic
python3 scripts/check_public_tree.py
echo 'Sender, native firmware logic and public-tree checks passed.'
