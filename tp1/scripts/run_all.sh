#!/usr/bin/env bash
# Compile, lance les 3 pipelines (serial, pthread, tbb) puis compare leurs images avec check-pipelines.
#
#   ./scripts/run_all.sh
#   DATA_DIR=data/synthetic ./scripts/run_all.sh
source "$(dirname "$0")/common.sh"

build pipeline check-pipelines
# On vérifie les images même si un pipeline échoue, mais on renvoie quand même une erreur.
status=0
run_pipeline pipeline all "$@" || status=$?
echo "==> Vérification des images"
"$BUILD_DIR/check-pipelines" "$DATA_DIR" || status=$?
exit "$status"
