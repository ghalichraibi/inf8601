#!/usr/bin/env bash
# Compile tout puis lance les tests ctest (image synthétique + comparaison des 3 pipelines).
#
#   ./scripts/test.sh
#   ./scripts/test.sh -R pthread     arguments passés à ctest (ici : seulement les tests contenant "pthread")
source "$(dirname "$0")/common.sh"

build
echo "==> Tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure "$@"
