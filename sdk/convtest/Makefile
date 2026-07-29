# convtest — rv32 soft-float sanity for the game's sample conversion.
GAME = convtest
GAME_SRCS = main.cpp compat_cxx.cpp libc_shim.c math_shim.c rv_shim.c
BUILD_DIR ?= $(CURDIR)/../../soc/build/pocket
ifneq ($(wildcard $(BUILD_DIR)/software/include/generated/variables.mak),)
include ../game.mk
OJ_CXXFLAGS = $(foreach f,$(filter-out -std=gnu99,$(CFLAGS)),\
                $(if $(findstring picolibc,$(f)),,$(f))) \
              -fno-threadsafe-statics -fno-use-cxa-atexit -std=gnu++17
GAME_CXXLIBS = -lstdc++ -lgcc -lm
endif
CFLAGS := -I$(CURDIR) $(CFLAGS)
