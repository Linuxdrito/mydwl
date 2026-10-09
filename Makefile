_VERSION = 0.8-dev
VERSION  = `git describe --tags --dirty 2>/dev/null || echo $(_VERSION)`

PKG_CONFIG = pkg-config

# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share

WLR_INCS = `$(PKG_CONFIG) --cflags wlroots-0.20`
WLR_LIBS = `$(PKG_CONFIG) --libs wlroots-0.20`

CC = cc

.POSIX:
.SUFFIXES:

DWLCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L -DVERSION=\"$(VERSION)\"

# CFLAGS / LDFLAGS
PKGS      = wayland-server xkbcommon libinput
DWLCFLAGS = -O3 -march=native -flto -fomit-frame-pointer -ffunction-sections -fdata-sections `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(DWLCPPFLAGS) $(CFLAGS)
LDFLAGS   = -s
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -Wl,--gc-section -lm $(LIBS)

all: mydwl
mydwl: mydwl.o util.o wallpaper.o
	$(CC) mydwl.o util.o wallpaper.o $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@
mydwl.o: mydwl.c client.h config.h cursor-shape-v1-protocol.h pointer-constraints-unstable-v1-protocol.h xdg-shell-protocol.h wallpaper.h
util.o: util.c util.h
wallpaper.o: wallpaper.c wallpaper.h util.h stb_image.h

WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

config.h:
	cp config.def.h $@
clean:
	rm -f mydwl *.o *-protocol.h config.h

install: mydwl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/mydwl
	cp -f mydwl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/mydwl

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c $<
