# MIDI PLAYER — Standard MIDI Files performed on the FM flavor's REAL OPL3.
#
#   make            -> midiplay.bin  (needs the SoC build tree; SD Game slot)
#   make pak        -> songs.pak    (.mid files + banks/ ; SD Pak slot)
#   make -f ../pc/pc.mk -> midiplay-pc  (desktop twin; RVSTACK_PAK=songs.pak)
#
GAME      = midiplay
GAME_SRCS = main.c smf.c bank.c oplgm.c
PORTLIB   = pakfs

.DEFAULT_GOAL := all

# assets/ -> songs.pak (see sdk/pakfs.h; banks/ holds OPL patch banks)
pak: songs.pak
songs.pak: $(wildcard assets/*) $(wildcard assets/banks/*)
	python3 ../../soc/tools/make_pakfs.py assets songs.pak

.PHONY: pak

BUILD_DIR ?= ../../soc/build/pocket
ifneq ($(wildcard $(BUILD_DIR)/software/include/generated/variables.mak),)
include ../game.mk
else
all:
	@echo "midiplay: console build needs the SoC build tree at $(BUILD_DIR)"
	@echo "          (build the SoC first, or use the PC twin:)"
	@echo "          make -f ../pc/pc.mk && RVSTACK_PAK=songs.pak ./midiplay-pc"
	@false
endif
