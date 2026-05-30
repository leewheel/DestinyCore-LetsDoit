#!/usr/bin/env bash
#
# Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
#
# Scaffolds a new module from skeleton-module: copies the template and rewires
# the loader entry point so the result builds without further edits.

set -euo pipefail

MODULES_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE_DIR="${MODULES_DIR}/skeleton-module"

read -rp "Enter the name of your future module: " MODULE_NAME

while [[ -z "${MODULE_NAME}" || "${MODULE_NAME}" == *" "* ]]; do
  echo "The name must be non-empty and contain no spaces."
  read -rp "Enter the name of your future module: " MODULE_NAME
done

TARGET_DIR="${MODULES_DIR}/${MODULE_NAME}"

if [[ -e "${TARGET_DIR}" ]]; then
  echo "A module named '${MODULE_NAME}' already exists."
  exit 1
fi

echo "--- Copying skeleton-module to ${MODULE_NAME}/"
cp -r "${TEMPLATE_DIR}" "${TARGET_DIR}"

# The loader function is the directory name with '-' turned into '_'.
LOADER_SUFFIX="${MODULE_NAME//-/_}"

echo "--- Rewiring the loader entry point"
sed -i "s/Addskeleton_moduleScripts/Add${LOADER_SUFFIX}Scripts/g" "${TARGET_DIR}/src/MP_loader.cpp"

echo "--- Done. Reconfigure CMake to pick up modules/${MODULE_NAME}."
