#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RISCV_CC="${RISCV_CC:-riscv64-unknown-elf-gcc}"
SPIKE_BIN="${SPIKE_BIN:-spike}"

for tool in "$RISCV_CC" "$SPIKE_BIN" grep timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Required tool not found: $tool" >&2
        exit 2
    fi
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
ELF="$WORK_DIR/vector_subset_reference.elf"
COMMANDS="$WORK_DIR/spike-debug.commands"
LOG="$WORK_DIR/spike.log"

"$RISCV_CC" \
    -march=rv64gcv \
    -mabi=lp64d \
    -nostdlib \
    -nostartfiles \
    -ffreestanding \
    -Wl,-T,"$SCRIPT_DIR/link.ld" \
    -Wl,--no-relax \
    -Wl,--build-id=none \
    -o "$ELF" \
    "$SCRIPT_DIR/vector_subset_reference.S"

# Stop when the final reduction result is committed to s0. This is an
# architectural completion condition, so it does not depend on ELF symbol
# addresses or the simulator's initial PC mapping.
cat > "$COMMANDS" <<'EOF'
# Advance a bounded number of instructions. The reference program parks in
# its _done loop, so every result register is stable after the finite kernel.
# This avoids relying on interactive "until" state polling.
rs 1000
reg 0 a0
reg 0 a1
reg 0 a2
reg 0 a3
reg 0 a4
reg 0 a5
reg 0 a6
reg 0 a7
reg 0 s0
quit
EOF

# Spike's interactive until loop advances in bounded instruction batches. A
# timeout remains as a guard in case the expected result is never produced.
set +e
timeout --signal=TERM --kill-after=5s 60s \
    "$SPIKE_BIN" --isa=rv64gcv -d --debug-cmd="$COMMANDS" "$ELF" >"$LOG" 2>&1
SPIKE_STATUS=$?
set -e
if [[ "$SPIKE_STATUS" -ne 0 ]]; then
    echo "Spike exited with status $SPIKE_STATUS; debugger output follows:" >&2
    cat "$LOG" >&2
    exit "$SPIKE_STATUS"
fi

mapfile -t ACTUAL < <(grep -E '^0x[[:xdigit:]]{16}$' "$LOG" | tr '[:upper:]' '[:lower:]')
EXPECTED=(
    "0x000000000000000c"
    "0xffffffffffffffeb"
    "0xfffffffffffffff8"
    "0x000000000000000f"
    "0xfffffffffffffffa"
    "0x0000000000000009"
    "0xfffffffffffffff4"
    "0xfffffffffffffff1"
    "0xfffffffffffffffe"
)
NAMES=(a0 a1 a2 a3 a4 a5 a6 a7 s0)

if [[ "${#ACTUAL[@]}" -ne "${#EXPECTED[@]}" ]]; then
    echo "Spike returned ${#ACTUAL[@]} register values; expected ${#EXPECTED[@]}." >&2
    cat "$LOG" >&2
    exit 1
fi

for index in "${!EXPECTED[@]}"; do
    if [[ "${ACTUAL[$index]}" != "${EXPECTED[$index]}" ]]; then
        echo "Spike reference mismatch in ${NAMES[$index]}: expected ${EXPECTED[$index]}, got ${ACTUAL[$index]}" >&2
        cat "$LOG" >&2
        exit 1
    fi
    printf '%-3s %s\n' "${NAMES[$index]}" "${ACTUAL[$index]}"
done

echo "Spike RVV reference check passed (VLEN=128, ELEN=64, SEW=32, AVL=4)."
