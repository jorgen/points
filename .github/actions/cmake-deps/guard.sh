#!/usr/bin/env bash
#
# Cache guard for a cmake-dep 3rdparty/ tree.
#
#   guard.sh key    --manifest F --epoch E --profile P --os O [--bootstrap CMakeLists.txt]
#   guard.sh verify --manifest F [--dir 3rdparty] [--enumerate enumerate.cmake]
#   guard.sh stamp  --manifest F [--dir 3rdparty] [--enumerate enumerate.cmake]
#   guard.sh digest --manifest F [--dir 3rdparty]
#
# Why this exists: cmake-dep's entire "already fetched" signal is `if (NOT EXISTS "${dir}")`.
# A directory that exists but is empty, truncated, or holds a *different* revision than the pin now
# names is indistinguishable from a good one, and cmake-dep will skip it forever. That is survivable
# when the tree is local scratch; it stops being survivable once the tree is a CI cache restored
# into every future run. `verify` is what makes a restored tree trustworthy, and it heals per
# dependency: a bad entry is deleted, and the next configure refetches exactly that one.
#
# The cache ROLLS FORWARD rather than being keyed on the pins. cmake-dep updates 3rdparty/ in place:
# bump a pin and the next configure fetches the one thing that moved, leaving everything else alone.
# So the key is a stable prefix plus a digest of the resulting tree — restore takes the newest entry
# under the prefix, and a new entry is only written when the tree genuinely changed. Keying on the
# packages file instead (what this repo did before) threw the whole 40 MB away on every edit,
# including edits to a comment.
#
# Written for bash 3.2 (macOS) with BSD/GNU find and Git-Bash on Windows: no associative arrays,
# no mapfile, no bare find -printf.

set -uo pipefail

STAMP=".cmdep-stamp"
# The fingerprint mode is part of the stamp version, so a tree stamped in one mode and verified in
# the other reports a version difference instead of looking like every dependency went bad at once.
# (It still costs a full refetch — the two digests are not comparable — but the log says why.)
STAMP_VERSION="v1$([ "${CMDEP_DEEP:-0}" = 1 ] && echo d || echo q)"

# Layout/semantics version of the cache itself, recorded inside the restored tree. Bump it to
# invalidate every existing cache without touching the key: a restored tree whose version file does
# not match is discarded wholesale and refetched, and the next save rolls the prefix forward. This is
# the lever to pull when the stamp format or the verification rules change under you.
CACHE_FORMAT="1"
VERSION_FILE=".cmdep-cache-version"

die() { printf 'cmake-deps: %s\n' "$*" >&2; exit 1; }
note() { printf 'cmake-deps: %s\n' "$*"; }

# ---------------------------------------------------------------- portability

if find . -maxdepth 0 -printf '' >/dev/null 2>&1; then FIND_MODE=gnu; else FIND_MODE=bsd; fi

# Resolved as a COMMAND, not a shell function: the deep-fingerprint path feeds it to xargs, and xargs
# execs a binary — it cannot see a function. A function here made CMDEP_DEEP=1 produce empty output
# on any host without a binary literally named `sha256`, which the fingerprint then read as "this
# directory has no files" and deleted every dependency in the tree.
if command -v sha256sum >/dev/null 2>&1; then
  SHA_CMD="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
  SHA_CMD="shasum -a 256"
else
  die "no sha256sum or shasum on PATH"
fi
sha256_stdin() { $SHA_CMD | cut -d' ' -f1; }

# ---------------------------------------------------------------- fingerprint

# Print "<files> <bytes> <digest>" for one dependency directory; fail if it holds no files.
#
# Nested 3rdparty/ and CMakeArtifacts/ subtrees are excluded. The nested one matters: a native build
# fetches vio's OWN dependencies into 3rdparty/vio-<sha>/3rdparty/, and folding those into vio's
# fingerprint would invalidate vio's 57 MB entry whenever an unrelated sub-dependency moved. Those
# nested trees are verified separately, against vio's own packages file.
fingerprint() {
  dir="$1"
  manifest=$(
    cd "$dir" || exit 1
    if [ "$FIND_MODE" = gnu ]; then
      find . \( -name 3rdparty -o -name CMakeArtifacts \) -prune -o \
           \( -type f -o -type l \) ! -name "$STAMP" -printf '%p\t%s\n'
    else
      find . \( -name 3rdparty -o -name CMakeArtifacts \) -prune -o \
           \( -type f -o -type l \) ! -name "$STAMP" -print0 |
        xargs -0 stat -f '%N	%z'
    fi | sed 's|^\./||' | LC_ALL=C sort
  ) || return 1
  [ -n "$manifest" ] || return 1

  files=$(printf '%s\n' "$manifest" | wc -l | tr -d ' ')
  bytes=$(printf '%s\n' "$manifest" | awk -F'\t' '{s+=$2} END {printf "%d", s+0}')

  # CMDEP_DEEP=1 upgrades the digest from path+size to path+size+content.
  #
  # Deliberately serial. `xargs -P N sha256sum` is NOT safe: sha256sum writing to a pipe is fully
  # buffered and flushes 4 KiB blocks whose boundaries land mid-line, so concurrent writers
  # interleave into corrupted lines that a trailing sort cannot repair. The digest comes out
  # nondeterministic — measured, not theorised.
  if [ "${CMDEP_DEEP:-0}" = 1 ]; then
    content=$(cd "$dir" && printf '%s\n' "$manifest" | cut -f1 | tr '\n' '\0' |
                xargs -0 $SHA_CMD | LC_ALL=C sort) || return 1
    [ -n "$content" ] || return 1
    manifest=$(printf '%s\n%s\n' "$manifest" "$content")
  fi

  printf '%s %s %s\n' "$files" "$bytes" "$(printf '%s\n' "$manifest" | sha256_stdin)"
}

# Every source tarball in this dependency set unpacks with at least one regular file at its root —
# a README, a LICENSE, a CMakeLists.txt or, for the single-header ones, the header. A directory with
# subdirectories but nothing at the top is a truncated extraction, and it is fatal in a specific way:
# add_subdirectory() on it fails with "does not contain a CMakeLists.txt file", and cmake-dep will
# never refetch it because the directory exists.
#
# The stamp we write ourselves sits at that root, so it has to be excluded — counting it would make
# this test vacuously true for every dependency that has ever been stamped.
has_root_files() {
  [ -n "$(find "$1" -maxdepth 1 -type f ! -name "$STAMP" ! -name "$VERSION_FILE" -print -quit 2>/dev/null)" ]
}

# ---------------------------------------------------------------- args

CMD="${1:-}"; shift || true
MANIFEST=""; DIR="3rdparty"; EPOCH=""; PROFILE=""; OSNAME=""; BOOTSTRAP=""; ENUMERATE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --manifest)  MANIFEST="$2"; shift 2 ;;
    --dir)       DIR="$2"; shift 2 ;;
    --epoch)     EPOCH="$2"; shift 2 ;;
    --profile)   PROFILE="$2"; shift 2 ;;
    --os)        OSNAME="$2"; shift 2 ;;
    --bootstrap) BOOTSTRAP="$2"; shift 2 ;;
    --enumerate) ENUMERATE="$2"; shift 2 ;;
    *) die "unknown argument: $1" ;;
  esac
done
[ -n "$MANIFEST" ] || die "--manifest is required"
[ -s "$MANIFEST" ] || die "manifest is missing or empty: $MANIFEST"

field() { printf '%s' "$1" | cut -d'|' -f"$2"; }

REMOVED=0
KEPT=0

# Strip everything that must never end up in a cache: CMakeArtifacts is a full CMake sub-build with
# absolute host paths baked in plus a second copy of every archive, and .fetch_tmp_* is an
# interrupted script-mode fetch (hidden, so a plain 3rdparty/* glob never sees it).
scrub() {
  find "$1" -type d -name CMakeArtifacts -prune -exec rm -rf {} + 2>/dev/null
  rm -rf "$1"/.fetch_tmp_* 2>/dev/null
}

# verify_root <dir> <manifest> <prune|noprune> [label]
verify_root() {
  root="$1"; man="$2"; mode="$3"; label="${4:-}"
  [ -d "$root" ] || return 0

  if [ "$mode" = prune ]; then
    # Without this, a restore-keys prefix hit accumulates every version the repo has ever pinned.
    # The local dev tree is the proof: 791 MB, 12 vio checkouts, three fmt versions, and two
    # packages that were dropped from the manifest long ago.
    expected=$(awk -F'|' '$1 != "SYSTEM" { print $4 }' "$man" | LC_ALL=C sort)
    for path in "$root"/*; do
      [ -e "$path" ] || continue
      name=$(basename "$path")
      if ! printf '%s\n' "$expected" | grep -qxF "$name"; then
        note "prune stale entry: ${label}${name}"
        rm -rf "$path"
        REMOVED=$((REMOVED + 1))
      fi
    done
  fi

  while IFS= read -r record; do
    [ -n "$record" ] || continue
    [ "$(field "$record" 1)" = "SYSTEM" ] && continue
    dirname=$(field "$record" 4)
    algo=$(field "$record" 5)
    hex=$(field "$record" 6)
    url=$(field "$record" 7)
    path="$root/$dirname"
    [ -d "$path" ] || continue

    if ! fp=$(fingerprint "$path") || ! has_root_files "$path"; then
      note "drop ${label}${dirname}: incomplete extraction"
      rm -rf "$path"; REMOVED=$((REMOVED + 1)); continue
    fi

    # No stamp means no successful configure ever blessed it — a tree from a pre-stamp cache, or one
    # saved by a run that died mid-fetch.
    if [ ! -f "$path/$STAMP" ]; then
      note "drop ${label}${dirname}: no stamp"
      rm -rf "$path"; REMOVED=$((REMOVED + 1)); continue
    fi

    want="$STAMP_VERSION|$algo|$hex|$url|$fp"
    got=$(cat "$path/$STAMP")
    if [ "$got" != "$want" ]; then
      # Either the pin moved under an unchanged directory name — cmakerc, argh and vio all carry a
      # truncated sha as their "version", so a re-pin need not rename the directory — or the tree no
      # longer matches what was fetched.
      note "drop ${label}${dirname}: stamp mismatch"
      note "  stamp says $got"
      note "  tree is    $want"
      rm -rf "$path"; REMOVED=$((REMOVED + 1)); continue
    fi

    KEPT=$((KEPT + 1))
  done < "$man"
}

# stamp_root <dir> <manifest> [label]
stamp_root() {
  root="$1"; man="$2"; label="${3:-}"
  [ -d "$root" ] || return 0
  while IFS= read -r record; do
    [ -n "$record" ] || continue
    [ "$(field "$record" 1)" = "SYSTEM" ] && continue
    dirname=$(field "$record" 4)
    algo=$(field "$record" 5)
    hex=$(field "$record" 6)
    url=$(field "$record" 7)
    path="$root/$dirname"

    if [ ! -d "$path" ]; then MISSING="$MISSING ${label}${dirname}"; continue; fi
    if ! fp=$(fingerprint "$path"); then MISSING="$MISSING ${label}${dirname}(empty)"; continue; fi
    printf '%s|%s|%s|%s|%s' "$STAMP_VERSION" "$algo" "$hex" "$url" "$fp" > "$path/$STAMP"
    WROTE=$((WROTE + 1))
  done < "$man"
}

# A dependency that carries its own cmake-dep packages file fetches its own dependencies into
# <dep>/3rdparty (vio does: libuv, LibreSSL, ada, structify, cmakerc — about a third of the cached
# bytes). Enumerate that file straight out of the fetched tree and apply the same rules inside it.
#
# Never prune by name down there. Stale nested versions cannot accumulate: any change to vio's own
# pins arrives with a new vio revision, which renames vio-<sha> and takes the whole nested tree with
# it. Pruning against a manifest enumerated under different options would only delete live entries.
#
# for_each_nested <verify|stamp>
for_each_nested() {
  action="$1"
  [ -n "$ENUMERATE" ] || return 0
  command -v cmake >/dev/null 2>&1 || { note "cmake not on PATH — nested trees left unverified"; return 0; }
  tmp="${TMPDIR:-/tmp}/cmdep-nested.$$"
  mkdir -p "$tmp"

  while IFS= read -r record; do
    [ -n "$record" ] || continue
    [ "$(field "$record" 1)" = "PACKAGE" ] || continue
    name=$(field "$record" 2)
    # Deliberately not named `dirname`: verify_root and stamp_root assign that global in their own
    # loops (bash 3.2, no `local` discipline here), so it holds the LAST nested entry by the time we
    # get back — which is how the nested diagnostics ended up naming the wrong parent.
    parent=$(field "$record" 4)
    dep="$DIR/$parent"
    pkgfile="$dep/CMake/3rdPartyPackages.cmake"
    [ -d "$dep/3rdparty" ] && [ -f "$pkgfile" ] || continue

    nested_man="$tmp/$name.txt"
    if ! cmake -DCMDEP_PACKAGES_FILE="$pkgfile" -DCMDEP_ENUM_OUT="$nested_man" \
               -DCMDEP_PROJECT="$name" -P "$ENUMERATE" >/dev/null 2>&1 || [ ! -s "$nested_man" ]; then
      note "WARNING: could not enumerate $parent's own packages file — its nested tree is UNVERIFIED"
      continue
    fi

    if [ "$action" = verify ]; then
      verify_root "$dep/3rdparty" "$nested_man" noprune "$parent/"
      # Anything nested that this project's enumeration does not know about — an option-gated dep,
      # say — still gets the cheap structural check, since an unusable one is just as fatal.
      for path in "$dep"/3rdparty/*; do
        [ -d "$path" ] || continue
        case "$(basename "$path")" in CMakeArtifacts) continue ;; esac
        if ! has_root_files "$path"; then
          note "drop $parent/$(basename "$path"): incomplete extraction"
          rm -rf "$path"; REMOVED=$((REMOVED + 1))
        fi
      done
    else
      stamp_root "$dep/3rdparty" "$nested_man" "$parent/"
    fi
  done < "$MANIFEST"

  rm -rf "$tmp"
}

case "$CMD" in

# ---------------------------------------------------------------- key
#
# Emits the stable PREFIX only. Restore asks for the prefix and takes the newest entry under it; the
# save key is the prefix plus a digest of the tree that configure actually produced (see 'digest').
#
# What is in the prefix, and why:
#   epoch    a tracked file, bumped by hand to walk away from every existing entry
#   profile  an Emscripten build never triggers vio's nested fetch, so its tree is a strict subset of
#            a native one; sharing a prefix would have the two overwrite each other in turn
#   os       runner families do not share a cache archive format
#   pin      the cmake-dep revision decides the on-disk LAYOUT, so a bump must not land on a tree
#            laid out the old way. Only that one line of CMakeLists.txt is read — unrelated edits to
#            the root CMakeLists.txt must not cost the cache.
key)
  [ -n "$EPOCH" ] || die "--epoch is required for 'key'"
  [ -n "$PROFILE" ] || die "--profile is required for 'key'"
  [ -n "$OSNAME" ] || die "--os is required for 'key'"

  pin="none"
  if [ -n "$BOOTSTRAP" ] && [ -f "$BOOTSTRAP" ]; then
    found=$(awk '/cmake-dep/ { seen = 1 } seen && /GIT_TAG/ { print $2; exit }' "$BOOTSTRAP")
    [ -n "$found" ] && pin="$(printf '%s' "$found" | cut -c1-12)"
  fi

  printf 'prefix=cmdep-%s-%s-%s-%s-\n' "$EPOCH" "$PROFILE" "$OSNAME" "$pin"
  ;;

# ---------------------------------------------------------------- digest
#
# A content address for the tree as it stands: the stamps of every verified dependency, top level and
# nested. Used as the save key's suffix, so a run only mints a new cache entry when the dependency
# tree actually moved — a pin bump, a dropped package, a healed corruption. On the overwhelming
# majority of runs the digest equals the one that was restored and nothing is uploaded at all.
digest)
  stamps=$(find "$DIR" -name "$STAMP" 2>/dev/null | LC_ALL=C sort)
  if [ -z "$stamps" ]; then
    printf 'digest=empty\n'
    exit 0
  fi
  printf 'digest=%s\n' "$(
    printf '%s\n' "$stamps" | while IFS= read -r s; do
      [ -n "$s" ] && printf '%s\t%s\n' "${s#"$DIR"/}" "$(cat "$s")"
    done | LC_ALL=C sort | { printf 'cmdep-tree-%s\n' "$CACHE_FORMAT"; cat; } | sha256_stdin
  )"
  ;;

# ---------------------------------------------------------------- verify
#
# Runs on a restored (or absent) tree, BEFORE configure. Anything it cannot vouch for is deleted
# rather than repaired — the next configure refetches it from the pinned URL under its recorded hash,
# which is the only source of truth available.
verify)
  # This subcommand deletes. On a runner that is the entire point; on a laptop it silently costs
  # someone their working tree and a few hundred megabytes of refetch, so make them say so.
  if [ "${CI:-}" != "true" ] && [ "${CMDEP_ALLOW_LOCAL:-}" != "1" ]; then
    die "verify deletes unverifiable directories under $DIR. Outside CI, set CMDEP_ALLOW_LOCAL=1 to confirm."
  fi
  if [ ! -d "$DIR" ]; then
    note "no $DIR/ to verify (cold cache)"
    exit 0
  fi

  # Whole-tree invalidation, checked before anything else. A tree written by a different version of
  # this guard cannot be reasoned about — its stamps may mean something else, or not exist at all —
  # so it is discarded rather than inspected. This is also the one-time migration path for the caches
  # that predate stamping entirely.
  found_format=""
  [ -f "$DIR/$VERSION_FILE" ] && found_format=$(tr -d '[:space:]' < "$DIR/$VERSION_FILE")
  if [ "$found_format" != "$CACHE_FORMAT" ]; then
    if [ -n "$(find "$DIR" -type f -print -quit 2>/dev/null)" ]; then
      note "cache format '${found_format:-none}' != '$CACHE_FORMAT' — discarding the whole tree"
      rm -rf "$DIR"
    fi
    exit 0
  fi

  scrub "$DIR"
  verify_root "$DIR" "$MANIFEST" prune
  for_each_nested verify
  note "verified $KEPT, removed $REMOVED"
  # Machine-readable, because the save step needs it. A restored entry whose stamps are internally
  # consistent but whose payload was bad gets healed here and then re-fetched to a tree with exactly
  # the digest that bad entry was saved under — so the save would be skipped as "unchanged" and the
  # bad entry would be restored and healed again, every run, forever. Keys are immutable, so it
  # cannot be overwritten; the save step instead writes the healed tree under a distinct key, which
  # being newer wins the next prefix lookup.
  printf 'removed=%s\n' "$REMOVED"
  ;;

# ---------------------------------------------------------------- stamp
#
# Runs after a SUCCESSFUL configure, immediately before the cache is saved. A successful configure is
# what proves cmake-dep populated everything the build needs; stamping an unconfigured tree would
# bless a partial fetch and then preserve it forever.
stamp)
  [ -d "$DIR" ] || die "nothing to stamp: $DIR does not exist"
  scrub "$DIR"
  WROTE=0; MISSING=""
  stamp_root "$DIR" "$MANIFEST"
  for_each_nested stamp
  # Not fatal. A declared package can legitimately be absent — an Emscripten build never triggers
  # vio's nested fetch at all — so say it loudly rather than failing the job.
  [ -n "$MISSING" ] && note "not stamped (absent after configure):$MISSING"
  printf '%s\n' "$CACHE_FORMAT" > "$DIR/$VERSION_FILE"
  note "stamped $WROTE entries (cache format $CACHE_FORMAT)"
  ;;

*)
  die "usage: guard.sh {key|verify|stamp} --manifest F [--dir D] [--enumerate E] [--epoch E --profile P --os O]"
  ;;
esac
