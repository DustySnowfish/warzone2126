#!/bin/sh
# Warzone 2126: checks that deformable terrain (craters, earthworks, hull-down cover) is
# deterministic, so multiplayer games and replays cannot desync on it.
#
# Runs the artillery and battle scenarios with each given binary and compares the per-tick sync
# CRC traces. Crater, earthwork and cover changes are written to syncDebug(), so any difference in
# terrain between runs or between compilers shows up as a differing tick.
#
# Usage: terrainsync.sh <binary> [<binary> ...]   (for example the clang and gcc builds)
# With one binary it compares two runs of that binary.
set -e
[ $# -ge 1 ] || { echo "usage: $0 <binary> [<binary> ...]" >&2; exit 2; }
OUT=${OUT:-/tmp/terrainsync}
mkdir -p "$OUT"
status=0
for scenario in perf_artillery perf_battle; do
	first=""
	i=0
	for bin in "$@" ${2:-$1}; do
		[ $# -ge 2 ] && [ "$i" -ge $# ] && break
		trace="$OUT/$scenario.$i.txt"
		"$bin" --movementbench="$scenario" --movementarrangement=0 --gamestate-crc-trace="$trace" >/dev/null 2>&1
		if [ -z "$first" ]; then
			first="$trace"
		elif cmp -s "$first" "$trace"; then
			echo "$scenario run $i IDENTICAL ($(wc -l < "$trace" | tr -d ' ') ticks)"
		else
			tick=$(diff "$first" "$trace" | sed -n 's/^< \([0-9]*\) .*/\1/p' | head -1)
			echo "$scenario run $i DIFFERS at tick ${tick:-?}"
			status=1
		fi
		i=$((i + 1))
	done
done
exit $status
