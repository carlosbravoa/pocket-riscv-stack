# RISC-V Stack SDK (standalone export)

Build games for the RISC-V Stack Pocket core with nothing but
xpack riscv-none-elf-gcc on PATH:

    cd examples/pong && make      # -> pong.bin
    # copy to the Pocket SD card, pick it in the core's Game slot

Start with GUIDE.md. New game: copy examples/pong, edit its Makefile
(GAME / GAME_SRCS) and go.
