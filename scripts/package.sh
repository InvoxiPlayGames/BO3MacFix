#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

version="$(sed -n 's/#define MACFIX_VERSION "\(.*\)"/\1/p' source/versioninfo.h)"
archive="release/BO3MacFix-$version.tar.gz"

make
make -C installer
make test

rm -rf staging release
mkdir -p staging/BO3MacFix release
cp BO3MacFix.dylib installer/Install_BO3MacFix launcher/launch-bo3macfix staging/BO3MacFix/
chmod 0644 staging/BO3MacFix/BO3MacFix.dylib
chmod 0755 staging/BO3MacFix/Install_BO3MacFix staging/BO3MacFix/launch-bo3macfix

(cd staging && tar --no-mac-metadata --disable-copyfile --exclude='.DS_Store' -czf "../$archive" BO3MacFix)
(cd release && shasum -a 256 "$(basename "$archive")" > "$(basename "$archive").sha256")
cat "$archive.sha256"
