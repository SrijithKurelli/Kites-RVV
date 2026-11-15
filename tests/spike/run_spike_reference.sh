#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RISCV_CC="${RISCV_CC:-riscv64-unknown-elf-gcc}"
RISCV_NM="${RISCV_NM:-riscv64-unknown-elf-nm}"
SPIKE_BIN="${SPIKE_BIN:-spike}"

for tool in "$RISCV_CC" "$RISCV_NM" "$SPIKE_BIN" grep awk; do
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

DONE_PC="$("$RISCV_NM" -n "$ELF" | awk '$3 == "_done" { print $1; exit }')"
if [[ -z "$DONE_PC" ]]; then
    echo "Could not locate _done in the linked Spike reference ELF" >&2
    exit 2
fi

cat > "$COMMANDS" <<EOF
until pc 0 $DONE_PC
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

# Spike's V profile implies Zve64d and Zvl128b: ELEN=64, VLEN=128.
"$SPIKE_BIN" --isa=rv64gcv -d --debug-cmd="$COMMANDS" "$ELF" >"$LOG" 2>&1

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
