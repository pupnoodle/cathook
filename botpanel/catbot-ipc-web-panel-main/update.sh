#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
npm install
./node_modules/.bin/browserify script.js -o public/bundle.js.tmp
mv -f public/bundle.js.tmp public/bundle.js
