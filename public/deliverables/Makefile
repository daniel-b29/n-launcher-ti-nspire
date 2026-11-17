# ============================================================================
# N-Launcher — Makefile for TI-Nspire CX II-T (Ndless SDK) & Host Test Suite
# Modular Architecture: scanner, covers, storage, ui_state, animation, renderer
# ============================================================================

DEBUG     = FALSE
GCC       = nspire-gcc
AS        = nspire-as
GXX       = nspire-g++
LD        = nspire-ld
GENZEHN   = genzehn
MAKEPRG   = make-prg
HOST_CC   = gcc

GCCFLAGS  = -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -isystem /usr/include/newlib
LDFLAGS   = -Wl,--gc-sections
ZEHNFLAGS = --name "N-Launcher" \
            --author "daniel.b" \
            --version 3 \
            --notice "Console Game Library Launcher for TI-Nspire CX II-T (Build 2026-11-17 \#R7)" \
            --ndless-min 36 \
            --240x320-support true

HOTKEY_ZEHNFLAGS = --name "nl_hotkey" \
                   --author "daniel.b" \
                   --version 1 \
                   --notice "N-Launcher Experimental TI-OS Global Hotkey (Ctrl+Menu)" \
                   --ndless-min 36 \
                   --240x320-support true

ifeq ($(DEBUG),FALSE)
	GCCFLAGS += -Os
else
	GCCFLAGS += -O0 -g
endif

EXE       = N-Launcher
DISTDIR   = .
CORE_SRCS = scanner.c covers.c storage.c profiles.c ui_state.c animation.c renderer.c
CORE_OBJS = $(CORE_SRCS:.c=.o)
OBJS      = main_ndless.o $(CORE_OBJS)

.PHONY: all test clean

all: $(DISTDIR)/$(EXE).tns nl_hotkey.tns

nl_hotkey.o: nl_hotkey.c boot_guard_core.h
	$(GCC) -Wall -Wextra -marm -std=c11 -fdata-sections -isystem /usr/include/newlib -Os -c $< -o $@

nl_hotkey.elf: nl_hotkey.o
	$(LD) $^ -o $@ $(LDFLAGS) -Wl,-u,_malloc_r

nl_hotkey.tns: nl_hotkey.elf
	$(GENZEHN) --input $< --output $@.zehn $(HOTKEY_ZEHNFLAGS)
	$(MAKEPRG) $@.zehn $@
	rm -f $@.zehn

hotkey_stub_data.h: nl_hotkey.tns
	python3 -c "import sys; d=open('nl_hotkey.tns','rb').read(); open('hotkey_stub_data.h','w').write('/* Auto-generated compiled ARM hotkey resident module */\n#ifndef HOTKEY_STUB_DATA_H\n#define HOTKEY_STUB_DATA_H\n#include <stdint.h>\n#include <stddef.h>\nstatic const uint8_t G_HOTKEY_STUB_TNS[] = {' + ','.join(f'0x{b:02x}' for b in d) + '};\nstatic const size_t G_HOTKEY_STUB_TNS_LEN = sizeof(G_HOTKEY_STUB_TNS);\n#endif\n')"

%.o: %.c nlauncher.h
	$(GCC) $(GCCFLAGS) -c $< -o $@

main_ndless.o: main_ndless.c nlauncher.h boot_guard_core.h
	$(GCC) $(GCCFLAGS) -c $< -o $@

profiles.o: profiles.c nlauncher.h avatars_data.h
	$(GCC) $(GCCFLAGS) -c $< -o $@

renderer.o: renderer.c nlauncher.h cursors_data.h
	$(GCC) $(GCCFLAGS) -c $< -o $@

storage.o: storage.c nlauncher.h boot_guard_core.h hotkey_stub_data.h
	$(GCC) $(GCCFLAGS) -c $< -o $@

$(DISTDIR)/$(EXE).elf: $(OBJS)
	mkdir -p $(DISTDIR)
	$(LD) $^ -o $@ $(LDFLAGS)

$(DISTDIR)/$(EXE).tns: $(DISTDIR)/$(EXE).elf
	$(GENZEHN) --input $< --output $@.zehn $(ZEHNFLAGS)
	$(MAKEPRG) $@.zehn $@
	rm -f $@.zehn

test: $(DISTDIR)/$(EXE).tns hotkey_stub_data.h
	$(HOST_CC) -Wall -Wextra -std=c11 -O2 -D_DEFAULT_SOURCE test_runner.c $(CORE_SRCS) -o test_runner
	./test_runner $(DISTDIR)/$(EXE).tns

clean:
	rm -f *.o $(DISTDIR)/$(EXE).elf $(DISTDIR)/$(EXE).tns $(DISTDIR)/$(EXE).tns.zehn nl_hotkey.elf nl_hotkey.tns nl_hotkey.tns.zehn test_runner frame_preview.bmp frame_zoom_preview.bmp

