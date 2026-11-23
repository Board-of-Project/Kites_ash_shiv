#!/usr/bin/env bash
#
# Parallel differential fuzzing & metrics collection loop for Kites + reorder_tool.
#
# Usage:   ./tester.sh [iterations] [--branch] [--loop] [--ctrl <pct>] [--no-subword] [--no-w] [--with-profile|--no-profile]
# Example: env JOBS=8 FWD=1 PROFILE=1 ITER_LEN=500 MEM_PCT=25 TIMEOUT=30 ./tester.sh 500 --branch
#
# Settings (environment variables or CLI flags, all optional):
#   --branch        enable random forward branches & jal (sets CTRL_PCT=15 if unset)
#   --loop          enable jump-isolated bounded countdown loops (sets CTRL_PCT=15 if unset)
#   --ctrl <pct>    set percentage of control-flow instructions (0-100, default 0)
#   --no-subword    disable sub-word loads/stores (lw/sw/lh/sh/lb/sb) in generator
#   --no-w          disable RV64I 32-bit *W ALU ops (addw/subw/addiw/etc.) in generator
#   --with-profile  profile on rvss to detect independent ld/sd pairs before reordering (PROFILE=1, default)
#   --no-profile    disable ld/sd profiling and keep all ld/sd pairs dependent (PROFILE=0)
#   JOBS            parallel worker processes            default: all logical CPUs
#   KITES           kites-cli binary                     default: auto-detected ../build-cli/kites-cli or ../../build-cli/kites-cli
#   GEN             random generator binary              default: $SCRIPT_DIR/rv64i_gen
#   GEN_CMD         custom generator command             default: unset
#   REORDER         reordering tool binary               default: $SCRIPT_DIR/reorder_tool
#   COMB            data_collector binary                default: $SCRIPT_DIR/data_collector
#   FAIL_DIR        failing test artifacts directory     default: /tmp/comp_arch/fail
#   WORK_DIR        per-worker scratch directory         default: /tmp/comp_arch/work
#   CYCLES1/2       cycle logs for data_collector        default: /tmp/comp_arch/cycles1.txt, cycles2.txt
#   FULL_CSV        full architectural metrics CSV       default: /tmp/comp_arch/full_metrics.csv
#   FWD             forwarding mode (1=on, 0=off)        default: 1
#   PROFILE         ld/sd dependency profiling (1=on, 0=off) default: 1
#   STATS_VM        VM for cycle/IPC/stall measurement   default: rv5-h-f (if FWD=1) or rv5-h-nf (if FWD=0)
#   VM              VM for functional oracle check       default: matches STATS_VM
#   ITER_LEN        random instructions per program      default: 300
#   MEM_PCT         percentage of load/store instrs      default: 25
#   CTRL_PCT        percentage of branch/jal/loop instrs default: 0 (15 when --branch or --loop is passed)
#   SEED            base random seed                     default: random
#   TIMEOUT         seconds per tool run (0=disabled)    default: 0
#   STOP_ON_FAIL    stop all workers on first failure    default: 0
#   SAVE_REGRESSIONS save cases where cycle2 > cycle1   default: 0

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REORDER_DIR="$SCRIPT_DIR/../reordering"
ITERATIONS=""
BRANCH="${BRANCH:-0}"
CTRL_PCT="${CTRL_PCT:-}"
PROFILE="${PROFILE:-1}"
GEN_EXTRA_FLAGS="${GEN_EXTRA_FLAGS:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --branch|--ctrl-on)
            BRANCH=1
            shift
            ;;
        --ctrl|--ctrl-pct)
            if [ $# -lt 2 ]; then
                echo "error: $1 requires a percentage (0-100)" >&2; exit 2
            fi
            CTRL_PCT="$2"
            shift 2
            ;;
        --ctrl=*)
            CTRL_PCT="${1#*=}"
            shift
            ;;
        --with-profile|--profile)
            PROFILE=1
            shift
            ;;
        --no-profile|--static)
            PROFILE=0
            shift
            ;;
        --loop|--no-subword|--no-w)
            if [ "$1" = "--loop" ]; then
                BRANCH=1
            fi
            GEN_EXTRA_FLAGS="$GEN_EXTRA_FLAGS $1"
            shift
            ;;
        -h|--help)
            echo "usage: $0 [iterations] [--branch] [--loop] [--ctrl <pct>] [--no-subword] [--no-w] [--with-profile|--no-profile]" >&2; exit 0
            ;;
        *)
            if [[ "$1" =~ ^[0-9]+$ ]] && [ -z "$ITERATIONS" ]; then
                ITERATIONS="$1"
                shift
            else
                echo "usage: $0 [iterations] [--branch] [--loop] [--ctrl <pct>] [--no-subword] [--no-w] [--with-profile|--no-profile]" >&2; exit 2
            fi
            ;;
    esac
done

ITERATIONS="${ITERATIONS:-5}"
if [ -z "$CTRL_PCT" ]; then
    if [ "$BRANCH" = "1" ]; then
        CTRL_PCT=15
    else
        CTRL_PCT=0
    fi
fi

JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
if [ -z "${KITES:-}" ]; then
    if [ -x "$SCRIPT_DIR/../build-cli/kites-cli" ]; then
        KITES="$SCRIPT_DIR/../build-cli/kites-cli"
    else
        KITES="$SCRIPT_DIR/../../build-cli/kites-cli"
    fi
fi

GEN="${GEN:-$SCRIPT_DIR/rv64i_gen}"
COMB="${COMB:-$SCRIPT_DIR/data_collector}"
GEN_CMD="${GEN_CMD:-}"
COMB_CMD="${COMB_CMD:-}"
REORDER="${REORDER:-$SCRIPT_DIR/reorder_tool}"
FAIL_DIR="${FAIL_DIR:-/tmp/comp_arch/fail}"
WORK_DIR="${WORK_DIR:-/tmp/comp_arch/work}"
CYCLES1="${CYCLES1:-/tmp/comp_arch/cycles1.txt}"
CYCLES2="${CYCLES2:-/tmp/comp_arch/cycles2.txt}"
FULL_CSV="${FULL_CSV:-/tmp/comp_arch/full_metrics.csv}"
FWD="${FWD:-1}"
MEM_PCT="${MEM_PCT:-25}"
if [ -z "${STATS_VM:-}" ]; then
    if [ "$FWD" = "1" ]; then
        STATS_VM="rv5-h-f"
    else
        STATS_VM="rv5-h-nf"
    fi
fi
VM="${VM:-$STATS_VM}"
ITER_LEN="${ITER_LEN:-300}"
TIMEOUT="${TIMEOUT:-0}"
STOP_ON_FAIL="${STOP_ON_FAIL:-0}"
SEED="${SEED:-$(( (RANDOM << 15 | RANDOM) ))}"

if ! [[ "$ITERATIONS" =~ ^[0-9]+$ ]] || [ "$ITERATIONS" -lt 1 ]; then
    echo "usage: $0 [iterations] [--branch] [--loop] [--ctrl <pct>] [--with-profile|--no-profile]" >&2; exit 2
fi
[ "$JOBS" -lt 1 ] && JOBS=1
[ "$JOBS" -gt "$ITERATIONS" ] && JOBS="$ITERATIONS"
[ "$(uname)" = "Darwin" ] && JOBS=1

TIMEOUT_CMD=""
if [ "$TIMEOUT" -gt 0 ]; then
    if command -v timeout >/dev/null 2>&1; then TIMEOUT_CMD="timeout $TIMEOUT"
    elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT_CMD="gtimeout $TIMEOUT"
    else echo "warning: TIMEOUT set but no timeout/gtimeout command found; ignoring" >&2; fi
fi

# Build generator on demand (and auto-rebuild when rv64i.cpp is updated).
if [ -z "$GEN_CMD" ] && [ -f "$SCRIPT_DIR/rv64i.cpp" ] && { [ ! -x "$GEN" ] || [ "$SCRIPT_DIR/rv64i.cpp" -nt "$GEN" ]; }; then
    echo "building generator from $SCRIPT_DIR/rv64i.cpp -> $GEN"
    g++ -std=c++17 -O2 "$SCRIPT_DIR/rv64i.cpp" -o "$GEN" || { echo "generator build failed" >&2; exit 2; }
fi

# Build reorder_tool on demand against sources/headers in ../reordering.
need_reorder_build=0
if [ -f "$SCRIPT_DIR/reorder_tool.cpp" ]; then
    if [ ! -x "$REORDER" ] || [ "$SCRIPT_DIR/reorder_tool.cpp" -nt "$REORDER" ]; then
        need_reorder_build=1
    else
        for src in "$REORDER_DIR"/*.cpp "$REORDER_DIR"/*.h; do
            if [ -f "$src" ] && [ "$src" -nt "$REORDER" ]; then
                need_reorder_build=1
                break
            fi
        done
    fi
fi

if [ "$need_reorder_build" = "1" ]; then
    echo "building reorder tool from $SCRIPT_DIR/reorder_tool.cpp -> $REORDER"
    g++ -std=c++17 -O2 -I"$REORDER_DIR" \
        "$SCRIPT_DIR/reorder_tool.cpp" \
        "$REORDER_DIR/parser.cpp" \
        "$REORDER_DIR/DAG.cpp" \
        "$REORDER_DIR/scheduler.cpp" \
        "$REORDER_DIR/heurestic.cpp" \
        -o "$REORDER" || { echo "reorder tool build failed" >&2; exit 2; }
fi

tools=("$KITES" "$REORDER")
[ -z "$GEN_CMD" ] && tools+=("$GEN")
for tool in "${tools[@]}"; do
    if [ ! -x "$tool" ]; then echo "error: '$tool' not found or not executable" >&2; exit 2; fi
done

# Resolve binaries to absolute paths so workers can safely cd into isolated directories.
abs_path() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
KITES="$(abs_path "$KITES")"
REORDER="$(abs_path "$REORDER")"
[ -z "$GEN_CMD" ] && GEN="$(abs_path "$GEN")"

rm -rf "$WORK_DIR"
mkdir -p "$FAIL_DIR" "$WORK_DIR" "$(dirname "$CYCLES1")" "$(dirname "$CYCLES2")" "$(dirname "$FULL_CSV")" || exit 2
: > "$CYCLES1"; : > "$CYCLES2"

now_ns() {
    local t
    t=$(date +%s%N 2>/dev/null)
    if [[ "$t" =~ ^[0-9]+$ ]]; then
        echo "$t"
    else
        python3 -c 'import time; print(int(time.time() * 1e9))' 2>/dev/null || echo 0
    fi
}

# Strict permutation validator: ensures order.txt is a bijection over 0..(N-1)
# with zero dropped, duplicated, non-numeric, or out-of-range instruction indices.
validate_permutation() {
    local code_file="$1" order_file="$2"
    awk '
        NR == FNR { n++; next }
        {
            if ($0 !~ /^[0-9]+$/ || NF != 1) {
                printf("non-integer index at line %d: %s", FNR, $0)
                bad = 1; exit 1
            }
            idx = $1 + 0
            if (idx < 0 || idx >= n) {
                printf("out-of-bounds index %d at line %d (valid 0..%d)", idx, FNR, n - 1)
                bad = 1; exit 1
            }
            if (seen[idx]++) {
                printf("duplicate instruction index %d at line %d", idx, FNR)
                bad = 1; exit 1
            }
            m++
        }
        END {
            if (!bad && m != n) {
                printf("instruction count mismatch: expected %d, got %d", n, m)
                exit 1
            }
        }
    ' "$code_file" "$order_file"
}

run_kites() {
    local asm="$1" dir="$2" vm_state="$3" mem_bases="$4"
    local cmd=("$KITES" "$asm")
    [ -n "$VM" ] && cmd+=(--vm "$VM")

    rm -rf "$dir"; mkdir -p "$dir"
    rm -f "$vm_state/registers_dump.json" "$vm_state/memory_dump.json"

    $TIMEOUT_CMD "${cmd[@]}" >"$dir/stdout" 2>"$dir/stderr"
    echo $? > "$dir/rc"

    sed '/^Assembled /,$d' "$dir/stdout" > "$dir/output.txt"
    cp "$vm_state/registers_dump.json" "$dir/regs.json" 2>/dev/null || : > "$dir/regs.json"
    cp "$vm_state/memory_dump.json" "$dir/mem.json" 2>/dev/null || : > "$dir/mem.json"

    local pmem_cmds=""
    for b in $mem_bases; do
        pmem_cmds+="pmem ${b} 8\n"
    done

    # Run REPL 'pmem' (across all base addresses) and 'info' in an isolated directory
    (
        cd "$dir" && printf "load %s --vm %s\nrun\n${pmem_cmds}info\nexit\n" "$asm" "$STATS_VM" \
            | $TIMEOUT_CMD "$KITES" >"$dir/info.txt" 2>/dev/null
    )
    grep -E '0x[0-9a-fA-F]+:' "$dir/info.txt" > "$dir/pmem.txt" || : > "$dir/pmem.txt"
    awk -v cyc_out="$dir/cycles.txt" -v stat_out="$dir/stats.csv" '
        /^Cycles:/       { c=$2 }
        /^IPC:/          { ipc=$2 }
        /^CPI:/          { cpi=$2 }
        /^Stall cycles:/ { stalls=$3 }
        END {
            if (c != "" && ipc != "") {
                print "Cycles : " c " IPC " ipc > cyc_out
                print c "," ipc "," (cpi != "" ? cpi : 0) "," (stalls != "" ? stalls : 0) > stat_out
            } else {
                print "0,0,0,0" > stat_out
            }
        }
    ' "$dir/info.txt"
}

run_worker() {
    local wid="$1"
    local wdir="$WORK_DIR/worker_$wid"
    local vm_state
    if [ "$(uname)" = "Darwin" ]; then
        vm_state="$HOME/Library/Application Support/Kites/vm_state"
    else
        vm_state="$wdir/vm_state"
    fi

    mkdir -p "$wdir"
    cd "$wdir" || exit 2

    local code1="$wdir/code1.s"
    local code2="$wdir/code2.s"
    local run1="$wdir/run1"
    local run2="$wdir/run2"
    local w_cyc1="$wdir/cycles1.txt"
    local w_cyc2="$wdir/cycles2.txt"
    local w_metrics="$wdir/metrics.csv"
    : > "$w_cyc1"; : > "$w_cyc2"; : > "$w_metrics"

    local w_pass=0 w_fail=0

    for ((i = wid; i < ITERATIONS; i += JOBS)); do
        [ "$STOP_ON_FAIL" = "1" ] && [ -f "$WORK_DIR/stop" ] && break

        local SEED_I=$((SEED + i))
        local reason="" detail=""
        local mem_bases="0x10000000"
        local first_base="0x10000000"
        local dep_file="$wdir/load_store_dep.txt"
        rm -f "$dep_file"

        # 1. generator
        if [ -n "$GEN_CMD" ]; then
            export SEED_I CODE1="$code1" ITER_LEN MEM_PCT CTRL_PCT
            bash -c "$GEN_CMD"
        else
            "$GEN" "${ITER_LEN:-200}" "$SEED_I" "$MEM_PCT" "$CTRL_PCT" $GEN_EXTRA_FLAGS > /dev/null
            cp "$wdir/rv64i_assembly.txt" "$code1"
            cp "$wdir/rv64i_instructions.txt" "${code1}.hex"
            if [ -s "$wdir/rv64i_mem_base.txt" ]; then
                mem_bases="$(tr '\n' ' ' < "$wdir/rv64i_mem_base.txt")"
                first_base="${mem_bases%% *}"
            fi
        fi
        local grc=$?
        if [ $grc -ne 0 ]; then
            reason="generator_error"; detail="generator exited with code $grc"
        elif [ ! -s "$code1" ]; then
            reason="generator_no_output"; detail="generator did not write $code1"
        fi

        # 2. kites on code1
        if [ -z "$reason" ]; then
            run_kites "$code1" "$run1" "$vm_state" "$mem_bases"
            if [ ! -s "$run1/regs.json" ]; then
                reason="kites_error_on_code1"
                detail="kites-cli failed to produce registers_dump.json.
$(tail -n 5 "$run1/stderr")"
            fi
        fi

        # 2.5. dynamic LD-SD dependency profiling on rvss (when PROFILE=1)
        local indep_pairs=0
        local profile_ms="0.000"
        if [ -z "$reason" ]; then
            if [ "$PROFILE" = "1" ]; then
                local p_start p_end
                p_start=$(now_ns)
                $TIMEOUT_CMD "$KITES" "$code1" --vm rvss --profile --profile-out "$dep_file" >"$wdir/profile.stdout" 2>"$wdir/profile.stderr"
                local prc=$?
                p_end=$(now_ns)
                if [ "$p_start" -gt 0 ] && [ "$p_end" -ge "$p_start" ]; then
                    profile_ms=$(awk -v s="$p_start" -v e="$p_end" 'BEGIN { printf "%.3f", (e - s) / 1000000.0 }')
                fi
                if [ $prc -ne 0 ] || [ ! -f "$dep_file" ]; then
                    reason="kites_profile_error"
                    detail="kites-cli --profile exited with code $prc (124 = timeout)
$(tail -n 5 "$wdir/profile.stderr")"
                else
                    indep_pairs=$(wc -l < "$dep_file" | tr -d ' ')
                fi
            else
                # Prevent DAG.cpp from falling back to a stale ../load_store_dep.txt
                dep_file="/dev/null"
            fi
        fi

        # 3. reordering tool (timed) + strict permutation check
        local reorder_ms="0.000"
        if [ -z "$reason" ]; then
            local t_start t_end
            t_start=$(now_ns)
            KITES_LD_SD_FILE="$dep_file" $TIMEOUT_CMD "$REORDER" "${code1}.hex" "$FWD" "$wdir/order.txt" >"$wdir/reorder.stdout" 2>"$wdir/reorder.stderr"
            local rrc=$?
            t_end=$(now_ns)
            if [ "$t_start" -gt 0 ] && [ "$t_end" -ge "$t_start" ]; then
                reorder_ms=$(awk -v s="$t_start" -v e="$t_end" 'BEGIN { printf "%.3f", (e - s) / 1000000.0 }')
            fi

            if [ $rrc -ne 0 ]; then
                reason="reorder_tool_error"
                detail="$REORDER exited with code $rrc (124 = timeout)
$(tail -n 5 "$wdir/reorder.stderr")"
            else
                local perm_err
                perm_err="$(validate_permutation "$code1" "$wdir/order.txt")"
                if [ $? -ne 0 ]; then
                    reason="invalid_permutation"
                    detail="order.txt is not a valid bijection of input instructions: $perm_err"
                else
                    awk 'NR==FNR { a[NR-1] = $0; next } { print a[$1] }' "$code1" "$wdir/order.txt" > "$code2"
                    if [ ! -s "$code2" ]; then
                        reason="reorder_no_output"; detail="reordering tool did not write $code2"
                    fi
                fi
            fi
        fi

        # 4. kites on code2
        if [ -z "$reason" ]; then
            run_kites "$code2" "$run2" "$vm_state" "$mem_bases"
            if [ ! -s "$run2/regs.json" ]; then
                reason="kites_error_on_code2"
                detail="kites-cli failed to produce registers_dump.json.
$(tail -n 5 "$run2/stderr")"
            fi
        fi

        # 5. compare output, registers, and memory (memory_dump.json + pmem across all 4 base pointers)
        if [ -z "$reason" ]; then
            local d_out d_reg d_mem d_pmem
            d_out="$(diff "$run1/output.txt" "$run2/output.txt")"
            d_reg="$(diff "$run1/regs.json" "$run2/regs.json")"
            d_mem="$(diff "$run1/mem.json" "$run2/mem.json")"
            d_pmem="$(diff "$run1/pmem.txt" "$run2/pmem.txt")"
            if [ ! -s "$run1/regs.json" ] || [ ! -s "$run2/regs.json" ]; then
                reason="missing_register_dump"; detail="registers_dump.json missing for one of the runs ($vm_state)"
            elif [ -n "$d_out" ] || [ -n "$d_reg" ] || [ -n "$d_mem" ] || [ -n "$d_pmem" ]; then
                reason="output_mismatch"
                detail="--- program output diff (code1 vs code2) ---
${d_out:-<identical>}
--- register diff (code1 vs code2) ---
${d_reg:-<identical>}
--- memory dump diff (code1 vs code2) ---
${d_mem:-<identical>}
--- data segment pmem ($mem_bases) diff (code1 vs code2) ---
${d_pmem:-<identical>}"
            fi
        fi

        # Zero-cycle guard: catch REPL 'info' timeouts/parse failures so 0-cycle rows never pollute CSVs
        local s1="" s2="" c1=0 c2=0
        if [ -z "$reason" ]; then
            s1=$(cat "$run1/stats.csv")
            s2=$(cat "$run2/stats.csv")
            c1="${s1%%,*}"; c1="${c1:-0}"
            c2="${s2%%,*}"; c2="${c2:-0}"
            if ! [[ "$c1" =~ ^[0-9]+$ ]] || ! [[ "$c2" =~ ^[0-9]+$ ]] || [ "$c1" -le 0 ] || [ "$c2" -le 0 ]; then
                reason="kites_stats_error"
                detail="kites-cli REPL info failed to report positive cycle counts (c1=$c1, c2=$c2)"
            fi
        fi

        if [ -z "$reason" ]; then
            w_pass=$((w_pass + 1))
            [ -s "$run1/cycles.txt" ] && cat "$run1/cycles.txt" >> "$w_cyc1"
            [ -s "$run2/cycles.txt" ] && cat "$run2/cycles.txt" >> "$w_cyc2"

            local moved est_c1 est_c2 saved pct_saved
            moved=$(awk '$1 != NR-1' "$wdir/order.txt" | wc -l | tr -d ' ')
            read -r est_c1 est_c2 < "$wdir/reorder.stdout"
            echo "$SEED_I,$ITER_LEN,$MEM_PCT,$CTRL_PCT,$FWD,$STATS_VM,$s1,$s2,${est_c1:-0},${est_c2:-0},$moved,$reorder_ms,$PROFILE,$indep_pairs,$profile_ms" >> "$w_metrics"

            saved=$((c1 - c2))
            pct_saved=$(awk -v s="$saved" -v c="$c1" 'BEGIN { printf "%.2f%%", (s / c) * 100.0 }')

            if [ "$PROFILE" = "1" ]; then
                printf '[PASS] #%d (w%d) seed=%d mem=%s pairs=%s | cycles: %s -> %s (saved %d, %s)\n' \
                    "$i" "$wid" "$SEED_I" "$first_base" "$indep_pairs" "$c1" "$c2" "$saved" "$pct_saved"
            else
                printf '[PASS] #%d (w%d) seed=%d mem=%s | cycles: %s -> %s (saved %d, %s)\n' \
                    "$i" "$wid" "$SEED_I" "$first_base" "$c1" "$c2" "$saved" "$pct_saved"
            fi

            if [ "${SAVE_REGRESSIONS:-0}" = "1" ] && [ "$c2" -gt "$c1" ]; then
                local reg_dir="$FAIL_DIR/../regressions"
                mkdir -p "$reg_dir"
                local rbase="$reg_dir/seed_${SEED_I}_plus_$((c2 - c1))"
                cp "$code1" "$rbase.code1.s"
                cp "$code2" "$rbase.code2.s"
                cp "$wdir/order.txt" "$rbase.order.txt"
                [ -f "$wdir/load_store_dep.txt" ] && cp "$wdir/load_store_dep.txt" "$rbase.ld_sd.txt"
                diff -u "$run1/info.txt" "$run2/info.txt" > "$rbase.info.diff"
            fi
        else
            w_fail=$((w_fail + 1))
            local base="$FAIL_DIR/test_number_${i}_${reason}"
            cp "$code1" "$base.s" 2>/dev/null
            [ -s "$code2" ] && cp "$code2" "$base.code2.s"
            [ -f "$wdir/load_store_dep.txt" ] && cp "$wdir/load_store_dep.txt" "$base.ld_sd.txt"
            {
                echo "reason:    $reason"
                echo "seed:      $SEED_I   (regenerate: $GEN ${ITER_LEN:-200} $SEED_I $MEM_PCT $CTRL_PCT$GEN_EXTRA_FLAGS)"
                echo "mem_bases: $mem_bases"
                echo "fwd:       $FWD   profile: $PROFILE (pairs=$indep_pairs)   mem_pct: $MEM_PCT   ctrl_pct: $CTRL_PCT   vm: ${VM:-default}"
                echo "$detail"
            } > "$base.log"
            printf '[FAIL] #%d (w%d) seed=%d mem=%s  %s  -> %s\n' "$i" "$wid" "$SEED_I" "$first_base" "$reason" "$base.s"
            if [ "$STOP_ON_FAIL" = "1" ]; then
                : > "$WORK_DIR/stop"
                break
            fi
        fi
    done

    echo "$w_pass $w_fail" > "$wdir/counts"
}

echo "base seed $SEED | $ITERATIONS iteration(s) | $JOBS worker(s) | ${ITER_LEN} instr | mem=${MEM_PCT}% | ctrl=${CTRL_PCT}% | fwd=$FWD | profile=$PROFILE | vm=$VM | stats_vm=$STATS_VM"
echo "failures: $FAIL_DIR"
echo

for ((w = 0; w < JOBS; w++)); do
    run_worker "$w" &
done
wait

pass=0; fail=0
echo "seed,instr_count,mem_pct,ctrl_pct,fwd,vm,cycle1,ipc1,cpi1,stalls1,cycle2,ipc2,cpi2,stalls2,est_cycle1,est_cycle2,moved,reorder_ms,profile,indep_pairs,profile_ms" > "$FULL_CSV"
for ((w = 0; w < JOBS; w++)); do
    wdir="$WORK_DIR/worker_$w"
    if [ -f "$wdir/counts" ]; then
        read -r wp wf < "$wdir/counts"
        pass=$((pass + wp))
        fail=$((fail + wf))
    fi
    [ -s "$wdir/cycles1.txt" ] && cat "$wdir/cycles1.txt" >> "$CYCLES1"
    [ -s "$wdir/cycles2.txt" ] && cat "$wdir/cycles2.txt" >> "$CYCLES2"
    [ -s "$wdir/metrics.csv" ] && cat "$wdir/metrics.csv" >> "$FULL_CSV"
done

echo
echo "done: $pass passed, $fail failed (failing programs in $FAIL_DIR)"
if [ "$pass" -gt 0 ] && [ -s "$FULL_CSV" ]; then
    awk -F',' '
        NR > 1 {
            c1 += $7; c2 += $11; pairs += $20; n++
            diff = $7 - $11
            if (diff > 0) improved++
            else if (diff == 0) unchanged++
            else {
                regressed++
                if (diff < worst_diff) { worst_diff = diff; worst_seed = $1; worst_c1 = $7; worst_c2 = $11 }
            }
            if (diff > best_diff) { best_diff = diff; best_seed = $1 }
        }
        END {
            if (n > 0 && c1 > 0) {
                printf "cycles: avg %.1f -> %.1f | total saved: %d (avg %.1f / %.2f%%)\n", c1/n, c2/n, c1-c2, (c1-c2)/n, (c1-c2)/c1*100
                printf "outcomes: %d improved (%.1f%%), %d unchanged, %d regressed (best: +%d on seed %s)\n", improved+0, (improved/n)*100, unchanged+0, regressed+0, best_diff+0, best_seed
                if ($19 == 1) {
                    printf "ld-sd profile: avg %.1f independent load/store pairs per program\n", pairs/n
                }
                if (regressed > 0) {
                    printf "worst regression: %d cycles (%d -> %d) on seed %s\n", worst_diff, worst_c1, worst_c2, worst_seed
                }
            }
        }
    ' "$FULL_CSV"
fi
if [ -z "$COMB_CMD" ] && [ -f "$SCRIPT_DIR/data_collector.cpp" ] && { [ ! -x "$COMB" ] || [ "$SCRIPT_DIR/data_collector.cpp" -nt "$COMB" ]; }; then
    echo "building data collector from $SCRIPT_DIR/data_collector.cpp -> $COMB"
    g++ -std=c++20 -O2 "$SCRIPT_DIR/data_collector.cpp" -o "$COMB" || { echo "data collector build failed" >&2; exit 2; }
fi
"$COMB"
echo "basic data stored in /tmp/comp_arch/data.csv"
echo "full metrics stored in $FULL_CSV"

[ "$fail" -eq 0 ]