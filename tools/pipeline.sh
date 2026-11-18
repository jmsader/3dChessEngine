#!/usr/bin/env bash
# Generate self-play training data with gendata, then train a learned evaluation on it.
#
# Usage (from the project folder):
#   tools/pipeline.sh [options] [-- extra train.py options]
#   tools/pipeline.sh --help       (prints this)
#
#   --engine NAME     engine playing the self-play games   (default Alpha-Beta+Ord+Q)
#   --time MS         milliseconds per move                (default 10)
#   --games N         games per process                    (default 1500)
#   --model KIND      linear, mlp, or both                  (default both: linear, then mlp)
#   --train           train only: no new games, use the existing data files for this config
#   anything after -- is passed to tools/train.py, e.g.  -- --res_w 0.7
#
# The number of parallel gendata processes is PROCS below (default: the CPU core count);
# the learning rate and epochs are constants at the top of tools/train.py.
#
# Data goes to data/<engine>_<time>ms_seed<S>.csv (one file per process; logs alongside).
# New runs add to the existing data: seeds continue after the highest one already there,
# so nothing is overwritten, and training always uses every file for the config.
# Trained weights go to weights/<engine>_<time>ms_linear.txt and/or ..._mlp.txt.
#
# Example: PROCS processes x 1500 games at 10 ms, then both models:
#   tools/pipeline.sh --time 10 --games 1500
set -euo pipefail

# Parallel gendata processes: one per CPU core by default. Edit to use fewer
# (e.g. PROCS=6 to leave cores free while it runs).
PROCS=$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)

engine="Alpha-Beta+Ord+Q"
time_ms=10
games=1500
model=both
train_only=0
train_args=()

usage() { sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --engine) engine="$2"; shift 2 ;;
    --time) time_ms="$2"; shift 2 ;;
    --games) games="$2"; shift 2 ;;
    --model) model="$2"; shift 2 ;;
    --train) train_only=1; shift ;;
    --) shift; train_args=("$@"); break ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

# Use the project's venv if there is one, so torch etc. are found.
python=${PYTHON:-python3}
[[ -x venv/bin/python ]] && python=venv/bin/python

# On macOS, keep the computer awake while this runs.
keep_awake=()
command -v caffeinate >/dev/null && keep_awake=(caffeinate -i)

config="${engine}_${time_ms}ms"
# Continue after the highest existing seed for this config, e.g. data/<config>_seed12.csv -> 13.
seed_start=1
for f in data/"${config}"_seed*.csv; do
  [[ -e "$f" ]] || continue
  n="${f##*_seed}"; n="${n%.csv}"
  [[ "$n" =~ ^[0-9]+$ ]] && (( n >= seed_start )) && seed_start=$((n + 1))
done
seed_end=$((seed_start + PROCS - 1))

echo "== building"
make -s

if [[ $train_only -eq 0 ]]; then
  mkdir -p data
  echo "== generating data: $PROCS processes x $games games, $engine at $time_ms ms (seeds $seed_start-$seed_end)"
  echo "   progress: tail -f data/${config}_seed${seed_start}.log"
  pids=()
  for ((s = seed_start; s <= seed_end; s++)); do
    "${keep_awake[@]}" ./build-make/gendata --engine "$engine" --time "$time_ms" --games "$games" \
      --seed "$s" --out "data/${config}_seed${s}.csv" > "data/${config}_seed${s}.log" 2>&1 &
    pids+=($!)
  done
  failed=0
  for pid in "${pids[@]}"; do wait "$pid" || failed=1; done
  if [[ $failed -ne 0 ]]; then
    echo "a gendata process failed; see data/${config}_seed*.log" >&2
    exit 1
  fi
  for ((s = seed_start; s <= seed_end; s++)); do tail -n 1 "data/${config}_seed${s}.log"; done
fi

files=(data/"${config}"_seed*.csv)
if [[ ! -e "${files[0]}" ]]; then
  echo "no data files data/${config}_seed*.csv" >&2
  exit 1
fi

case "$model" in
  both) kinds=(linear mlp) ;;
  linear|mlp) kinds=("$model") ;;
  *) echo "--model must be linear, mlp or both" >&2; exit 2 ;;
esac
for kind in "${kinds[@]}"; do
  echo "== training $kind on ${#files[@]} files"
  "${keep_awake[@]}" "$python" tools/train.py "${files[@]}" --model-kind "$kind" ${train_args[@]+"${train_args[@]}"}
  echo "== done: weights/${config}_${kind}.txt"
done
