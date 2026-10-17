CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PKGS     = sdl2 SDL2_image SDL2_mixer SDL2_ttf
# Windows (MinGW-w64, native in MSYS2 or cross): `make CC=x86_64-w64-mingw32-gcc` also works. Kept apart from
# CFLAGS so `make CFLAGS=...` on the command line keeps them.
ifneq (,$(or $(findstring mingw,$(CC)),$(findstring Windows,$(OS))))
EXE         = .exe
PLAT_CFLAGS = -D__USE_MINGW_ANSI_STDIO=1 -D_USE_MATH_DEFINES
endif
# kept separate from CFLAGS so `make CFLAGS=...` on the command line still finds SDL
SDL_CFLAGS = $(shell pkg-config --cflags $(PKGS))
LDLIBS   = $(shell pkg-config --libs $(PKGS)) -lm
SRC      = $(wildcard src/*.c)

aimless$(EXE): $(SRC) $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(PLAT_CFLAGS) $(SDL_CFLAGS) $(SRC) -o $@ $(LDLIBS)

# test_plat (file system / process / zip-import layer, no shell) runs on every platform.
# test_skin (parser, file lookup, .osk import, circle-number layout) and test_skin_loader (src/skin.c on a fake SDL,
# see tests/sdlfake) drive the shell (mkdir, zip, rm), so they are POSIX only for now. None of them needs SDL.
# OSK=/path/to/skin.osk adds a real-skin run to the POSIX tests.
OSK ?=
TESTS_POSIX = tests/test_skin tests/test_skin_loader
ifneq (,$(EXE))
TESTS_POSIX =
endif
test: tests/test_plat$(EXE) tests/test_sliderbody$(EXE) tests/test_icons$(EXE) tests/test_mods$(EXE) tests/test_keybind$(EXE) $(TESTS_POSIX)
	./tests/test_plat$(EXE)
	./tests/test_sliderbody$(EXE)
	./tests/test_icons$(EXE)
	./tests/test_mods$(EXE)
	./tests/test_keybind$(EXE)
ifneq (,$(TESTS_POSIX))
	./tests/test_skin $(if $(OSK),"$(OSK)")
	./tests/test_skin_loader $(if $(OSK),"$(OSK)")
endif

tests/test_plat$(EXE): tests/test_plat.c tests/zipdata.h src/plat.c src/paths.c src/skinfs.c src/skinini.c src/beatmap.c src/library.c src/mapinfo.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(PLAT_CFLAGS) tests/test_plat.c src/plat.c src/paths.c src/skinfs.c src/skinini.c src/beatmap.c src/library.c src/mapinfo.c -o $@ -lm $(if $(EXE),-lshell32)

# slider body colour ramp / rasteriser against osu!lazer's constants (pure C, no SDL)
tests/test_sliderbody$(EXE): tests/test_sliderbody.c src/sliderbody.c src/sliderbody.h
	$(CC) $(CFLAGS) $(PLAT_CFLAGS) tests/test_sliderbody.c src/sliderbody.c -o $@ -lm

# gameplay mods: selection rules, difficulty adjustment, Hard Rock flip on a parsed beatmap (pure C, no SDL)
tests/test_mods$(EXE): tests/test_mods.c src/mods.c src/mods.h src/beatmap.c src/beatmap.h src/plat.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(PLAT_CFLAGS) tests/test_mods.c src/mods.c src/beatmap.c src/plat.c -o $@ -lm $(if $(EXE),-lshell32)

# gameplay key bindings (K1 / K2): validation, conflict rules and the settings.ini round trip (pure C, no SDL)
tests/test_keybind$(EXE): tests/test_keybind.c src/keybind.c src/keybind.h src/settings.c src/settings.h src/plat.c src/paths.c src/skinfs.c src/skinini.c src/beatmap.c src/library.c src/mapinfo.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(PLAT_CFLAGS) tests/test_keybind.c src/keybind.c src/settings.c src/plat.c src/paths.c src/skinfs.c src/skinini.c src/beatmap.c src/library.c src/mapinfo.c -o $@ -lm $(if $(EXE),-lshell32)

# procedural SF-Symbols-style icons (pure C, no SDL)
tests/test_icons$(EXE): tests/test_icons.c src/icons.c src/icons.h
	$(CC) $(CFLAGS) $(PLAT_CFLAGS) tests/test_icons.c src/icons.c -o $@ -lm

tests/test_skin: tests/test_skin.c src/plat.c src/paths.c src/skinfs.c src/skinini.c src/beatmap.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) tests/test_skin.c src/plat.c src/paths.c src/skinfs.c src/skinini.c src/beatmap.c -o $@ -lm

# runs the real src/skin.c against a fake SDL (tests/sdlfake): checks which files the circle digits load from, not rendering
tests/test_skin_loader: tests/test_skin_loader.c src/skin.c src/plat.c src/skinfs.c src/skinini.c tests/sdlfake/fake.c $(wildcard src/*.h) $(wildcard tests/sdlfake/*.h)
	$(CC) $(CFLAGS) -Itests/sdlfake tests/test_skin_loader.c src/skin.c src/plat.c src/skinfs.c src/skinini.c tests/sdlfake/fake.c -o $@ -lm

clean:
	rm -f aimless aimless.exe tests/test_plat tests/test_skin tests/test_skin_loader tests/test_sliderbody tests/test_icons tests/test_mods tests/test_keybind tests/*.exe

PREFIX ?= /usr/local
install: aimless$(EXE)
	install -Dm755 aimless$(EXE) $(DESTDIR)$(PREFIX)/bin/aimless$(EXE)

.PHONY: clean install test
