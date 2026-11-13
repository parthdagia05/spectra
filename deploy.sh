#!/usr/bin/env bash
# Build the static Spectra dashboard and deploy it to Cloudflare Pages.
# The deployed site runs in demo data mode since live scanning needs local ADB.
set -euo pipefail
cd "$(dirname "$0")"

PROJECT="${1:-spectra}"

rm -rf dist
mkdir -p dist
cp ui/dashboard.html dist/index.html
cp report.html dist/report.html
printf '/*    /index.html   200\n' > dist/_redirects

npx wrangler@latest pages deploy dist --project-name="$PROJECT" --branch=main --commit-dirty=true --force
