#!/usr/bin/env bash
# Mutation testing with Mull, restricted to the lines changed since <base-ref>.
#
#   scripts/mutation.sh <base-ref> [--suites a_test,b_test] [--dry-run] [--no-build]
#
# Builds every suite (or the named ones) with the Mull plugin in build/mutation,
# runs mull-runner on each, and folds the results into one verification line
# (scripts/mutation_summary.py). Mull's git diff filter keeps only the mutants on
# lines that differ from the merge base of <base-ref> and HEAD, uncommitted edits
# included. See CONTRIBUTING.md, "Mutation testing".
#
# It works from any directory: mull-runner is told its config through MULL_CONFIG
# and run from the build directory. Mull's diff filter skips a file the base ref
# does not have, so the filter's reference is a commit whose tree holds an empty copy of
# each such file under include/: every line of a new header then counts as
# changed. The script prints how many changed lines the filter will consider, and
# exits 3 when there are some but no mutant was run, 4 when Mull found no config
# or could not resolve the reference.
# With no changed C++ lines under include/ it says so and exits 0.
#
# The compiler must be the LLVM major version the Mull build was made for, so this
# does not use the clang-cxx20 preset's `clang++`. Defaults for macOS (Homebrew
# `llvm@21`, Mull unpacked under ~/.local/opt); override for Linux (apt `mull-21`):
#
#   CE_MULL_LLVM     LLVM prefix (bin/clang++)       /opt/homebrew/opt/llvm@21
#   CE_MULL_DIR      Mull prefix (bin, lib)          ~/.local/opt/mull-21
#   CE_MULL_VERSION  Mull's LLVM major version       21
#   CE_MULL_BUILD    build directory                 build/mutation

set -euo pipefail

usage() {
  echo "usage: scripts/mutation.sh <base-ref> [--suites a_test,b_test] [--dry-run] [--no-build]" >&2
  exit 2
}

[[ $# -ge 1 && $1 != -* ]] || usage
base_ref=$1
shift
suites=""
dry_run=0
no_build=0
while [[ $# -gt 0 ]]; do
  case $1 in
    --suites) [[ $# -ge 2 ]] || usage; suites=$2; shift 2 ;;
    --dry-run) dry_run=1; shift ;;
    --no-build) no_build=1; shift ;;
    *) usage ;;
  esac
done

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=${CE_MULL_VERSION:-21}
llvm=${CE_MULL_LLVM:-/opt/homebrew/opt/llvm@$version}
mull=${CE_MULL_DIR:-$HOME/.local/opt/mull-$version}
build=${CE_MULL_BUILD:-$root/build/mutation}
runner=$mull/bin/mull-runner-$version
plugin=$mull/lib/mull-ir-frontend-$version

# Exit codes beyond usage (2) and a missing tool (1).
readonly exit_no_mutants=3
readonly exit_no_config=4

merge_base=$(git -C "$root" merge-base "$base_ref" HEAD)

# Mull skips a file that is absent from the reference, so a header the change adds
# would be ignored whole. Hand it a tree where each such header exists, empty.
diff_tree() {
  local scratch empty tree
  scratch=$(mktemp -d)
  empty=$(git -C "$root" hash-object -w --stdin < /dev/null)
  git -C "$root" read-tree --index-output="$scratch/index" "$merge_base"
  git -C "$root" diff --name-only --diff-filter=A --no-renames -z "$merge_base" -- include/ |
    while IFS= read -r -d '' path; do
      printf '100644 %s 0\t%s\n' "$empty" "$path"
    done |
    GIT_INDEX_FILE="$scratch/index" git -C "$root" update-index --add --index-info
  tree=$(GIT_INDEX_FILE="$scratch/index" git -C "$root" write-tree)
  rm -rf "$scratch"
  # Mull resolves gitDiffRef to a commit, not a tree. The commit stays unreferenced
  # and git collects it; nothing in the working tree or the refs changes.
  GIT_AUTHOR_NAME=mutation.sh GIT_AUTHOR_EMAIL=mutation.sh@localhost \
    GIT_COMMITTER_NAME=mutation.sh GIT_COMMITTER_EMAIL=mutation.sh@localhost \
    git -C "$root" commit-tree "$tree" -m "mutation.sh: $merge_base plus empty copies of the headers added since"
}

# "<lines> <files>": C++ lines under include/ that differ from the diff tree.
changed_lines() {
  git -C "$root" diff -U0 --no-color --no-renames "$1" -- include/cloudevents |
    awk '
      /^\+\+\+ / { keep = ($2 ~ /\.(hpp|h|ipp|cppm)$/); if (keep) files++; next }
      /^@@ / && keep {
        n = $3; sub(/^\+[0-9]+,?/, "", n); if (n == "") n = 1
        lines += n
      }
      END { print lines + 0, files + 0 }'
}

ref=$(diff_tree)
read -r n_lines n_files < <(changed_lines "$ref")
config_file=$build/mull.yml

# The config Mull reads: the committed mull.yml plus the diff filter.
config() {
  cat "$root/mull.yml"
  echo "gitDiffRef: $ref"
  echo "gitProjectRoot: $root"
}

if [[ $n_lines -eq 0 ]]; then
  echo "mutation.sh: nothing to mutate: no changed C++ lines under include/ since $base_ref (merge base $merge_base)"
  echo "Mutation: none, because the change has no C++ lines under include/"
  exit 0
fi

if [[ $dry_run -eq 1 ]]; then
  echo "base ref:   $base_ref (merge base $merge_base)"
  echo "diff tree:  $ref"
  echo "changed:    $n_lines lines in $n_files files"
  echo "compiler:   $llvm/bin/clang++"
  echo "plugin:     $plugin"
  echo "build dir:  $build"
  echo "suites:     ${suites:-all}"
  echo "--- $config_file"
  config
  echo "--- per suite"
  echo "cwd:        $build"
  echo "command:    MULL_CONFIG=$config_file $runner --reporters IDE --ide-reporter-show-killed --allow-surviving <suite executable>"
  exit 0
fi

for needed in "$llvm/bin/clang++" "$runner" "$plugin"; do
  [[ -e $needed ]] || { echo "mutation.sh: missing $needed (see CONTRIBUTING.md, Mutation testing)" >&2; exit 1; }
done

mkdir -p "$build"
if [[ $no_build -eq 0 && ! -f $build/build.ninja ]]; then
  cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_CXX_COMPILER="$llvm/bin/clang++" \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DCE_WERROR=OFF -DCE_BUILD_TESTING=ON \
    "-DCMAKE_CXX_FLAGS=-O0 -g -grecord-command-line -fpass-plugin=$plugin"
fi
config > "$config_file"
echo "mutation.sh: $n_lines changed lines in $n_files files since $base_ref (merge base $merge_base)" >&2

if [[ -z $suites ]]; then
  suites=$(ctest --test-dir "$build" -N | sed -n 's/^ *Test *#[0-9]*: \(.*_test\)$/\1/p' | paste -sd, -)
fi
IFS=, read -r -a suite_list <<< "$suites"
[[ $no_build -eq 1 ]] || cmake --build "$build" --target "${suite_list[@]}"

logs=()
export MULL_CONFIG=$config_file
for suite in "${suite_list[@]}"; do
  log=$build/$suite.mull.log
  exe=$(find "$build" -type f -name "$suite" -perm -u+x | head -n 1)
  echo "== $suite" >&2
  (cd "$build" && "$runner" --reporters IDE --ide-reporter-show-killed --allow-surviving "$exe") > "$log" 2>&1 || true
  logs+=("$log")
done

if grep -q -e "Mull cannot find config" -e "GitDiffFilter: cannot resolve" "${logs[@]}"; then
  echo "mutation.sh: Mull did not read $config_file or could not resolve $ref (see ${logs[*]})" >&2
  exit "$exit_no_config"
fi

python3 "$root/scripts/mutation_summary.py" "${logs[@]}"

mutants=$(cat "${logs[@]}" | grep -c -E ': warning: (Killed|Survived):' || true)
if [[ $mutants -eq 0 ]]; then
  echo "mutation.sh: $n_lines lines changed under include/ but Mull ran no mutant on them." >&2
  echo "  Either no changed line has an operator Mull mutates (then write 'Mutation: none, because <reason>')," >&2
  echo "  or no suite in (${suites}) reaches the lines, or the build is stale: rebuild in $build and retry." >&2
  exit "$exit_no_mutants"
fi
