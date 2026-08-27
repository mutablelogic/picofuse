#!/bin/sh
# Doxygen INPUT_FILTER for README.md.
#
# GitHub renders README.md directly, so any Doxygen-only or Doxygen-excluded
# content has to be invisible to GitHub's markdown renderer too - plain HTML
# comments achieve that. This filter runs only when Doxygen builds the docs
# (see FILTER_PATTERNS in Doxyfile) and rewrites two kinds of marker before
# Doxygen parses the file:
#
#   <!-- DOXYGEN_EXCLUDE_START --> ... <!-- DOXYGEN_EXCLUDE_END -->
#       Content in between (e.g. the GitHub-only mermaid diagram) is
#       dropped entirely.
#
#   <!-- DOXYGEN_MODULES_DIAGRAM -->
#       Replaced with the contents of modules_diagram.dox.inc (a Doxygen
#       \dot graph), which GitHub never sees since it's just an HTML
#       comment there.
set -eu

file="$1"
here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
diagram="$here/modules_diagram.dox.inc"

awk -v diagram="$diagram" '
  /<!-- DOXYGEN_EXCLUDE_START -->/ { skip = 1; next }
  /<!-- DOXYGEN_EXCLUDE_END -->/   { skip = 0; next }
  skip { next }
  /<!-- DOXYGEN_MODULES_DIAGRAM -->/ {
    while ((getline line < diagram) > 0) print line
    close(diagram)
    next
  }
  { print }
' "$file"
