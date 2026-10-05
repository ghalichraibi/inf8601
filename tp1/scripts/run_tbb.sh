#!/usr/bin/env bash
# Compile puis lance le pipeline TBB.
#
#   ./scripts/run_tbb.sh                     sur data/downloaded/
#   DATA_DIR=data/synthetic ./scripts/run_tbb.sh
#   ASAN=1 ./scripts/run_tbb.sh              version AddressSanitizer
#   Les arguments supplémentaires sont passés à l'exécutable (ex. --quiet).
source "$(dirname "$0")/common.sh"

run_pipeline pipeline tbb "$@"
