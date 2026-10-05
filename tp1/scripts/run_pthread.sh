#!/usr/bin/env bash
# Compile puis lance le pipeline pthread (via pipeline-notbb, fonctionne même si le code TBB ne compile pas).
#
#   ./scripts/run_pthread.sh                      sur data/downloaded/
#   DATA_DIR=data/synthetic ./scripts/run_pthread.sh
#   ASAN=1 ./scripts/run_pthread.sh               version AddressSanitizer
#   Les arguments supplémentaires sont passés à l'exécutable (ex. --quiet).
source "$(dirname "$0")/common.sh"

run_pipeline pipeline-notbb pthread "$@"
