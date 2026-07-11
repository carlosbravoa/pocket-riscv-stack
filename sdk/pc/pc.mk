# PC twin: build any SDK game natively against desktop SDL2.
#   cd sdk/<game> && make -f ../pc/pc.mk        -> <game>-pc
# Keys: arrows, Z=A X=B A=X S=Y Q=L1 W=R1 TAB=SELECT ENTER=START, ESC quits.
# Pak file: ./game.pak or $RVSTACK_PAK. Saves: ./<name>.sav in the cwd.
PC_DIR   := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
SDK_DIR  := $(PC_DIR)/..

# pull GAME/GAME_SRCS/PORTLIB out of the game's own Makefile WITHOUT its rules
GAME      := $(shell sed -n 's/^GAME *= *//p'      Makefile)
GAME_SRCS := $(shell sed -n 's/^GAME_SRCS *= *//p' Makefile)
PORTLIB   := $(shell sed -n 's/^PORTLIB *= *//p'   Makefile)

SRCS  = $(GAME_SRCS) $(PORTLIB:%=$(SDK_DIR)/%.c) $(PC_DIR)/hal_pc.c
CC_PC ?= gcc
CFLAGS_PC = -O2 -g -Wall -I$(SDK_DIR) -I$(SDK_DIR)/../soc/hal \
            $(shell pkg-config --cflags sdl2) -DRVSTACK_PC
LIBS_PC   = $(shell pkg-config --libs sdl2)

$(GAME)-pc: $(SRCS) $(PC_DIR)/hal_pc.c
	$(CC_PC) $(CFLAGS_PC) -o $@ $(filter %.c,$^) $(LIBS_PC)

pc-clean:
	rm -f $(GAME)-pc
