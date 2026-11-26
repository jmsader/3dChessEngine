#!/usr/bin/env bash
# Run a match between any engines: builds first, keeps the computer awake, and saves the
# results to results/<ms>ms.csv (replaced by the next run at the same time control).
#
# Usage (from the project folder):
#   tools/compare.sh [options] ENGINE ENGINE [ENGINE ...]
#   tools/compare.sh [options] --all
#   tools/compare.sh --help        (prints this)
#
#   ENGINE        engine names as in the match tool: Random, Minimax, Alpha-Beta, Alpha-Beta+Ord,
#                 Alpha-Beta+Ord+Q, Alpha-Beta+Ord+Q+TT, and any search engine with
#                 +LIN_d<depth> / +NN_d<depth> for a learned evaluation: the linear / MLP model
#                 trained on data searched to <depth>, read from
#                 weights/Alpha-Beta+Ord+Q_d<depth>_linear.txt / _mlp.txt
#                 (e.g. Alpha-Beta+LIN_d2, Alpha-Beta+Ord+Q+NN_d2)
#   --time MS     milliseconds per move   (default 10)
#   --games N     games per pair          (default 4)
#   --all         ignore any engines named and play every engine: each search engine with
#                 material, +LIN_d2 and +NN_d2, plus Random (16 engines, 120 pairs)
#
# Results, in the results/ folder (created if missing):
#   results/<ms>ms.csv        rank, engine, elo, avg_depth
#   results/pairs/<ms>ms.csv  engine_a, engine_b, wins, draws, losses (counted for engine_a)
# e.g. results/10ms.csv. A run replaces the files for its time control only, so runs at
# different time controls can run at the same time.
#
# Examples:
#   tools/compare.sh Random Alpha-Beta+Ord+Q Alpha-Beta+Ord+Q+LIN_d2 Alpha-Beta+Ord+Q+NN_d2
#   tools/compare.sh Random Alpha-Beta+Ord+Q+NN_d1 Alpha-Beta+Ord+Q+NN_d2
#   tools/compare.sh --time 50 --games 10 Random Alpha-Beta Alpha-Beta+Ord Alpha-Beta+Ord+Q
set -euo pipefail

usage() { sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; }

time_ms=10
games=4
engines=()
all=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --time) time_ms="$2"; shift 2 ;;
    --games) games="$2"; shift 2 ;;
    --all) all=1; shift ;;
    -*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    *) engines+=("$1"); shift ;;
  esac
done
if [[ $all -eq 1 ]]; then
  engines=(--all)  # match picks the engines
elif [[ ${#engines[@]} -lt 2 ]]; then
  echo "name at least two engines" >&2; usage >&2; exit 2
fi


keep_awake=()
command -v caffeinate >/dev/null && keep_awake=(caffeinate -i)

make -s
${keep_awake[@]+"${keep_awake[@]}"} ./build-make/match "${engines[@]}" \
  --time "$time_ms" --games "$games" --csv "results/${time_ms}ms.csv"
