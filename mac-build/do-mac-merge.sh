#!/usr/bin/env bash
# Merge upstream macOS support (rexglue/rexglue-sdk PR #356) into the
# futari fork's clang-cl branch. Run from the SDK submodule dir.
#
# This is a HELPER for the merge step only — it does not build anything.
# Expect 2 conflicts (see STATUS.md): dynlib.h (trivial) + rex_app.cpp (small).
set -euo pipefail

SDK="$(cd "$(dirname "$0")/futari_recomp/thirdparty/rexglue-sdk" && pwd)"
cd "$SDK"
echo "SDK: $SDK"

# Make sure we have the PR and upstream main.
git fetch upstream
git fetch upstream pull/356/head:pr356 || true

# Work on the dedicated merge branch (already created off futari-clang-cl-fixes).
git switch futari-mac-merge

echo ">>> attempting merge of pr356 ..."
if git merge --no-ff pr356; then
  echo ">>> clean merge (unexpected — re-check conflicts list in STATUS.md)"
else
  echo
  echo ">>> conflicts (expected ~2). Resolve these files, then 'git add' + 'git commit':"
  git diff --name-only --diff-filter=U
fi
