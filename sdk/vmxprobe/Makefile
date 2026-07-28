# vmxprobe — audible CPU-stream vs vmx-voice A/B for hardware (see main.c).
# `make PROBE2=1` builds vmxprobe2 (main2.c): one-shot/looped long buffers +
# on-screen fetch-position telemetry.
ifdef PROBE2
GAME = vmxprobe2
GAME_SRCS = main2.c
else
GAME = vmxprobe
GAME_SRCS = main.c
endif
include ../game.mk
