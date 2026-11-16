#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RISCV_CC="${RISCV_CC:-riscv64-unknown-elf-gcc}"
SPIKE_BIN="${SPIKE_BIN:-spike}"
RISCV_NM="${RISCV_NM:-riscv64-unknown-elf-nm}"
RISCV_READELF="${RISCV_READELF:-riscv64-unknown-elf-readelf}"

for tool in "$RISCV_CC" "$RISCV_NM" "$RISCV_READELF" "$SPIKE_BIN" timeout; do
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

# Confirm that the linker retained the entry point and HTIF mailbox symbols.
SYMBOLS="$("$RISCV_NM" -n "$ELF")"
for symbol in _start tohost fromhost; do
    if ! grep -Eq "[[:space:]]${symbol}$" <<<"$SYMBOLS"; then
        echo "Reference ELF is missing required symbol: $symbol" >&2
        "$RISCV_READELF" -h "$ELF" >&2
        "$RISCV_NM" -n "$ELF" >&2
        exit 3
    fi
done

# Spike's reset ROM uses the configured start PC. Pass the ELF entry point
# explicitly so the bare-metal test never falls back to address zero when the
# ELF loader or toolchain's entry-point metadata differs.
ENTRY_POINT="$("$RISCV_READELF" -h "$ELF" | awk '/Entry point address:/ { print $4 }')"
if [[ ! "$ENTRY_POINT" =~ ^0x[[:xdigit:]]+$ ]] || [[ "$ENTRY_POINT" == "0x0" ]]; then
    echo "Invalid ELF entry point: ${ENTRY_POINT:-<empty>}" >&2
    "$RISCV_READELF" -h "$ELF" >&2
    exit 4
fi

# Run with a bounded instruction trace so failures include the last guest PCs.
set +e
timeout --signal=TERM --kill-after=5s 15s \
    "$SPIKE_BIN" --isa=rv64gcv --pc="$ENTRY_POINT" --disable-dtb -l --log="$LOG" "$ELF" >/dev/null 2>&1
SPIKE_STATUS=$?
set -e

if [[ "$SPIKE_STATUS" -ne 0 ]]; then
    echo "Spike reference program exited with status $SPIKE_STATUS." >&2
    echo "ELF entry point and HTIF symbols:" >&2
    "$RISCV_READELF" -h "$ELF" >&2
    "$RISCV_NM" -n "$ELF" | grep -E '(_start|tohost|fromhost)$' >&2 || true
    echo "Last 100 guest trace lines:" >&2
    tail -n 100 "$LOG" >&2
    exit "$SPIKE_STATUS"
fi

if grep -q "\*\*\* FAILED \*\*\*" "$LOG"; then
    echo "Spike reported a failed architectural result check:" >&2
    cat "$LOG" >&2
    exit 1
fi

echo "Spike RVV reference check passed (VLEN=128, ELEN=64, SEW=32, AVL=4)."
echo "Verified vmul.vv lanes [12,-21,-8,15], vmul.vx lanes [-6,9,-12,-15], and vredsum.vs result -2."
