#!/usr/bin/env bash
# Run a match between any engines: builds first, keeps the computer awake, and saves the
# results to results.csv (replaced by every run).
#
# Usage (from the project folder):
#   tools/compare.sh [options] ENGINE ENGINE [ENGINE ...]
#   tools/compare.sh --help        (prints this)
#
#   ENGINE        engine names as in the match tool: Random, Minimax, Alpha-Beta, Alpha-Beta+Ord,
#                 Alpha-Beta+Ord+Q, Alpha-Beta+Ord+Q+TT, and any search engine with
#                 +LIN_<ms> / +NN_<ms> for a learned evaluation: the linear / MLP model trained
#                 on <ms>-ms data, read from weights/Alpha-Beta+Ord+Q_<ms>ms_linear.txt / _mlp.txt
#                 (e.g. Alpha-Beta+LIN_10, Alpha-Beta+Ord+Q+NN_1)
#   --time MS     milliseconds per move   (default 100)
#   --games N     games per pair          (default 20)
#
# Results, in the project folder (each run replaces them):
#   results.csv        rank, engine, elo, avg_depth, avg_ms
#   results_pairs.csv  engine_a, engine_b, wins, draws, losses (counted for engine_a)
#
# Examples:
#   tools/compare.sh Random Alpha-Beta+Ord+Q Alpha-Beta+Ord+Q+LIN_10 Alpha-Beta+Ord+Q+NN_10
#   tools/compare.sh Random Alpha-Beta+Ord+Q+NN_1 Alpha-Beta+Ord+Q+NN_10
#   tools/compare.sh --time 50 --games 10 Random Alpha-Beta Alpha-Beta+Ord Alpha-Beta+Ord+Q
set -euo pipefail

usage() { sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; }

time_ms=5
games=4
engines=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --time) time_ms="$2"; shift 2 ;;
    --games) games="$2"; shift 2 ;;
    -*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    *) engines+=("$1"); shift ;;
  esac
done
if [[ ${#engines[@]} -lt 2 ]]; then
  echo "name at least two engines" >&2; usage >&2; exit 2
fi


keep_awake=()
command -v caffeinate >/dev/null && keep_awake=(caffeinate -i)

make -s
${keep_awake[@]+"${keep_awake[@]}"} ./build-make/match "${engines[@]}" \
  --time "$time_ms" --games "$games" --csv results.csv
