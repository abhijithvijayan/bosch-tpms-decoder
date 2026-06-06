#!/bin/sh
# Embeds web/config.html into the firmware by generating include/config_page.h
# (a PROGMEM C string). This keeps the page editable/previewable as a normal
# HTML file — open web/config.html directly in a browser, or
# web/config.html?host=<device-ip> to talk to a live board.
#
# Run from anywhere: `scripts/embed_html.sh`. PlatformIO runs it before every
# build via the scripts/embed_html.py shim (extra_scripts must be Python).
set -eu
cd "$(dirname "$0")/.."

src="web/config.html"
dst="include/config_page.h"

{
  echo "// Auto-generated from $src by scripts/embed_html.sh — DO NOT EDIT."
  echo "// Edit $src instead; this file is regenerated on every build."
  echo "#pragma once"
  echo "#include <pgmspace.h>"
  echo ""
  echo "static const char CONFIG_PAGE[] PROGMEM ="
  # each HTML line -> escaped C string literal:  "...\n"
  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/    "/' -e 's/$/\\n"/' "$src"
  echo ";"
} > "$dst.tmp"

# Only replace when the content changed, so untouched HTML doesn't dirty the
# header's mtime and trigger a pointless recompile of main.cpp.
if cmp -s "$dst.tmp" "$dst" 2>/dev/null; then
  rm "$dst.tmp"
else
  mv "$dst.tmp" "$dst"
  echo "embed_html.sh: regenerated $dst from $src"
fi
