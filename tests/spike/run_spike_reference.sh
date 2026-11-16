#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RISCV_CC="${RISCV_CC:-riscv64-unknown-elf-gcc}"
SPIKE_BIN="${SPIKE_BIN:-spike}"

for tool in "$RISCV_CC" "$SPIKE_BIN" timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Required tool not found: $tool" >&2
        exit 2
    fi
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
ELF="$WORK_DIR/vector_subset_reference.elf"
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

# The reference program checks every lane and the reduction result itself,
# then signals pass/fail through Spike's standard HTIF tohost mailbox.
set +e
timeout --signal=TERM --kill-after=5s 60s \
    "$SPIKE_BIN" --isa=rv64gcv "$ELF" >"$LOG" 2>&1
SPIKE_STATUS=$?
set -e

if [[ "$SPIKE_STATUS" -ne 0 ]]; then
    echo "Spike reference program exited with status $SPIKE_STATUS; output follows:" >&2
    cat "$LOG" >&2
    exit "$SPIKE_STATUS"
fi

if grep -q "\*\*\* FAILED \*\*\*" "$LOG"; then
    echo "Spike reported a failed architectural result check:" >&2
    cat "$LOG" >&2
    exit 1
fi

echo "Spike RVV reference check passed (VLEN=128, ELEN=64, SEW=32, AVL=4)."
echo "Verified vmul.vv lanes [12,-21,-8,15], vmul.vx lanes [-6,9,-12,-15], and vredsum.vs result -2."
