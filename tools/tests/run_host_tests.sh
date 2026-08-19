#!/usr/bin/env bash

set -euo pipefail

tools_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 -m unittest discover \
  -s "$tools_dir/tests" \
  -p 'test_*.py' \
  -v
