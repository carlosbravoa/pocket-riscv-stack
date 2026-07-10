# Source this: . env.sh  — sets up the riscv-stack toolchain environment
export RISCV_STACK_ROOT="/home/carlos/devel/fpga/riscv-stack"
export PATH="$HOME/tools/xpack-riscv-none-elf-gcc-14.2.0-3/bin:$PATH"
export PATH="$HOME/tools/sbt/bin:$PATH"
export PATH="$HOME/altera_lite/25.1std/quartus/bin:$PATH"
export PATH="$HOME/tools/ghdl-mcode-6.0.0-ubuntu24.04-x86_64/bin:$PATH"
# LiteX default riscv triple -> point it at our xpack toolchain
export CROSS_COMPILE=riscv-none-elf-
[ -f "$RISCV_STACK_ROOT/.venv/bin/activate" ] && . "$RISCV_STACK_ROOT/.venv/bin/activate"

# NOTE: the LiteX *source* tree lives in ./litex which SHADOWS the installed
# 'litex' package if you run python from the repo root. Always run SoC scripts
# from ./soc (or any dir without a 'litex' subdir).
if [ "$PWD" = "$RISCV_STACK_ROOT" ]; then
  echo "WARNING: you are in the repo root — ./litex shadows the installed litex package; run SoC scripts from ./soc" >&2
fi
