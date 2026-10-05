#!/usr/bin/env bash
# Compile puis lance le pipeline serial (via pipeline-notbb, fonctionne même si le code TBB ne compile pas).
#
#   ./scripts/run_serial.sh                      sur data/downloaded/
#   DATA_DIR=data/synthetic ./scripts/run_serial.sh
#   ASAN=1 ./scripts/run_serial.sh               version AddressSanitizer
#   Les arguments supplémentaires sont passés à l'exécutable (ex. --quiet).
source "$(dirname "$0")/common.sh"

run_pipeline pipeline-notbb serial "$@"
