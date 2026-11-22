#!/usr/bin/env bash
#
# Generate one random RV64I program, copy it to ../cli/example.s,
# build kites_cli if needed and run `kites-cli cli/example.s --profile`,
# then compile the reordering tool (parser + DAG + scheduler + heurestic),
# run it on the program,
# and write the reordered program to example_reordered.s (in this folder).
#
# Usage:   ./make_reordered.sh [seed]
# Example: ITER_LEN=200 CTRL_PCT=15 FWD=1 ./make_reordered.sh 42
#
# Optional environment variables:
#   ITER_LEN   random instructions in the program   default: 300
#   MEM_PCT    percentage of load/store instrs      default: 25
#   CTRL_PCT   percentage of branch/jal instrs      default: 0
#   FWD        forwarding mode (1=on, 0=off)        default: 1
#   GEN_FLAGS  extra generator flags (e.g. "--loop --no-w")

set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REORDER_DIR="$SCRIPT_DIR/../reordering"
CLI_DIR="$SCRIPT_DIR/../cli"
PROJECT_DIR="$SCRIPT_DIR/.."

GEN="${GEN:-$SCRIPT_DIR/rv64i_gen}"
REORDER="${REORDER:-$SCRIPT_DIR/reorder_tool}"

ITER_LEN="${ITER_LEN:-300}"
MEM_PCT="${MEM_PCT:-25}"
CTRL_PCT="${CTRL_PCT:-0}"
FWD="${FWD:-1}"
GEN_FLAGS="${GEN_FLAGS:-}"
SEED="${1:-${SEED:-$(( (RANDOM << 15 | RANDOM) ))}}"

ORIG_ASM="$CLI_DIR/example.s"               # original random program
ORDER_FILE="$SCRIPT_DIR/order.txt"          # new instruction order
FINAL_ASM="$SCRIPT_DIR/example_reordered.s" # final reordered program

# ---- 1. build the generator if needed ----
if [ -f "$SCRIPT_DIR/rv64i.cpp" ] && { [ ! -x "$GEN" ] || [ "$SCRIPT_DIR/rv64i.cpp" -nt "$GEN" ]; }; then
    echo "building generator -> $GEN"
    g++ -std=c++17 -O2 "$SCRIPT_DIR/rv64i.cpp" -o "$GEN"
fi

[ -x "$GEN" ] || { echo "error: '$GEN' not found or not executable" >&2; exit 2; }

# ---- 2. generate the random program (files land in this folder) ----
cd "$SCRIPT_DIR"
echo "generating: len=$ITER_LEN seed=$SEED mem=$MEM_PCT% ctrl=$CTRL_PCT%"
"$GEN" "$ITER_LEN" "$SEED" "$MEM_PCT" "$CTRL_PCT" $GEN_FLAGS > /dev/null

[ -s rv64i_assembly.txt ]     || { echo "error: generator did not write rv64i_assembly.txt" >&2; exit 2; }
[ -s rv64i_instructions.txt ] || { echo "error: generator did not write rv64i_instructions.txt" >&2; exit 2; }

# ---- 3. copy the generated assembly to ../cli/example.s ----
mkdir -p "$CLI_DIR"
cp rv64i_assembly.txt "$ORIG_ASM"
echo "original program  -> $ORIG_ASM"

# ---- 4. build kites_cli if build-cli is missing, then profile example.s ----
# (commands use paths relative to the project root, so run them from there)
(
    cd "$PROJECT_DIR"
    rm -f load_store_dep.txt
    if [ ! -d build-cli ]; then
        echo "build-cli not found: configuring and building kites_cli..."
        cmake -S . -B build-cli
        cmake --build build-cli --target kites_cli -j"$(nproc)"
    fi
    echo "profiling original program..."
    # </dev/null stops kites-cli waiting for input; timeout stops it hanging forever
    TO=""; command -v timeout >/dev/null 2>&1 && TO="timeout 60"
    $TO ./build-cli/kites-cli cli/example.s --profile </dev/null \
        || echo "warning: kites-cli --profile failed or timed out (exit $?); continuing" >&2
)

# ---- 5. compile the reordering tool (parser, DAG, scheduler, heurestic) ----
# always rebuilt, so edits to DAG.cpp / scheduler.cpp / etc. are picked up
if [ -f "$SCRIPT_DIR/reorder_tool.cpp" ]; then
    echo "compiling reorder tool -> $REORDER"
    g++ -std=c++17 -O2 -I"$REORDER_DIR" \
        "$SCRIPT_DIR/reorder_tool.cpp" \
        "$REORDER_DIR/parser.cpp" \
        "$REORDER_DIR/DAG.cpp" \
        "$REORDER_DIR/scheduler.cpp" \
        "$REORDER_DIR/heurestic.cpp" \
        -o "$REORDER"
fi
[ -x "$REORDER" ] || { echo "error: '$REORDER' not found or not executable" >&2; exit 2; }

# ---- 6. run the reordering tool on the hex instructions ----
echo "reordering (fwd=$FWD)..."
# run from this folder: DAG.cpp reads ../load_store_dep.txt, which step 4 wrote in the project root
"$REORDER" rv64i_instructions.txt "$FWD" "$ORDER_FILE"

# ---- 7. check order.txt is a valid permutation ----
awk '
    NR == FNR { n++; next }
    $0 !~ /^[0-9]+$/ || $1 >= n || seen[$1]++ { bad = 1; exit 1 }
    { m++ }
    END { if (bad || m != n) exit 1 }
' "$ORIG_ASM" "$ORDER_FILE" || { echo "error: $ORDER_FILE is not a valid permutation" >&2; exit 3; }

# ---- 8. apply the new order to the assembly -> final .s ----
awk 'NR == FNR { a[NR-1] = $0; next } { print a[$1] }' "$ORIG_ASM" "$ORDER_FILE" > "$FINAL_ASM"
echo "reordered program -> $FINAL_ASM"

moved=$(awk '$1 != NR-1' "$ORDER_FILE" | wc -l | tr -d ' ')
echo "done: $moved of $(wc -l < "$ORIG_ASM" | tr -d ' ') instructions moved"
