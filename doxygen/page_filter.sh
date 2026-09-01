#!/bin/sh
# Doxygen INPUT_FILTER for markdown files that need to be their own Related
# Page but can't rely on Doxygen's implicit markdown-to-page mechanism -
# notably, a file literally named README.md other than the one designated
# via USE_MDFILE_AS_MAINPAGE doesn't reliably become its own page (Doxygen
# treats it as directory-readme content instead), and an auto-generated
# page has no way to carry a one-line description for the Related Pages
# index.
#
# Turns the file's leading "# Title" line into an explicit \page command
# (bypassing the above), and an immediately-following
# "<!-- @brief: text -->" marker (invisible on GitHub) into a \brief command
# so the Related Pages index shows a description. Everything else in the
# file passes through unchanged, run only when Doxygen builds the docs (see
# FILTER_PATTERNS in Doxyfile).
#
# Usage: page_filter.sh <page-id> <file>
set -eu

page_id="$1"
file="$2"

awk -v id="$page_id" '
  NR == 1 && /^# / {
    title = $0
    sub(/^# */, "", title)
    print "\\page " id " " title
    next
  }
  /^<!-- @brief: .* -->$/ {
    brief = $0
    sub(/^<!-- @brief: */, "", brief)
    sub(/ *-->$/, "", brief)
    print ""
    print "\\brief " brief
    next
  }
  { print }
' "$file"
