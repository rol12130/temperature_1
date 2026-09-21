#!/usr/bin/env bash
# Change CONFIG_APP_PROJECT_VER dans sdkconfig, sans avoir à ouvrir le
# fichier et chercher la ligne à la main.
#
# Usage : ./bump_version.sh 1.0.2
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "Usage : ./bump_version.sh <nouvelle_version>"
    echo "Exemple : ./bump_version.sh 1.0.2"
    exit 1
fi

NEW_VERSION="$1"
SDKCONFIG="sdkconfig"

if [ ! -f "$SDKCONFIG" ]; then
    echo "❌ $SDKCONFIG introuvable dans ce dossier."
    echo "   Lance d'abord 'idf.py build' (ou 'idf.py menuconfig') une fois pour le générer, puis relance ce script."
    exit 1
fi

if ! grep -q '^CONFIG_APP_PROJECT_VER=' "$SDKCONFIG"; then
    echo "❌ CONFIG_APP_PROJECT_VER introuvable dans $SDKCONFIG (config inattendue ?)."
    exit 1
fi

OLD_VERSION=$(grep '^CONFIG_APP_PROJECT_VER=' "$SDKCONFIG" | sed -E 's/^CONFIG_APP_PROJECT_VER="(.*)"$/\1/')

# -i '' : syntaxe macOS (BSD sed). Sur Linux ce serait juste -i sans le ''.
sed -i '' -E "s/^CONFIG_APP_PROJECT_VER=\".*\"/CONFIG_APP_PROJECT_VER=\"${NEW_VERSION}\"/" "$SDKCONFIG"

echo "✅ Version changée : ${OLD_VERSION} → ${NEW_VERSION}"
echo "   Prochaine étape : idf.py build"
