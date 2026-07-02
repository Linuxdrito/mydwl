_VERSION = 0.8-dev
VERSION  = `git describe --tags --dirty 2>/dev/null || echo $(_VERSION)`

PKG_CONFIG = pkg-config

# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share

WLR_INCS = `$(PKG_CONFIG) --cflags wlroots-0.19`
WLR_LIBS = `$(PKG_CONFIG) --libs wlroots-0.19`

CC = cc

.POSIX:
.SUFFIXES:

# flags for compiling
DWLCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\"
DWLDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = wayland-server xkbcommon libinput
DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(DWLCPPFLAGS) $(DWLDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

all: mydwl
mydwl: mydwl.o util.o
	$(CC) mydwl.o util.o $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@
mydwl.o: mydwl.c client.h config.h cursor-shape-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h
util.o: util.c util.h

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

config.h:
	cp config.def.h $@
clean:
	rm -f mydwl *.o *-protocol.h config.h

dist: clean
	mkdir -p mydwl-$(VERSION)
	cp -R Makefile client.h config.def.h \
		config.mk protocols mydwl.c util.c util.h dwl.desktop \
		mydwl-$(VERSION)
	tar -caf dwl-$(VERSION).tar.gz dwl-$(VERSION)
	rm -rf mydwl-$(VERSION)

install: mydwl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/mydwl
	cp -f dwl $(DESTDIR)$(PREFIX)/mybin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/mydwl
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f dwl.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/mydwl.desktop
	chmod 644 $(DESTDIR)$(DATADIR)/wayland-sessions/mydwl.desktop
uninstall:
		$(DESTDIR)$(DATADIR)/wayland-sessions/mydwl.desktop

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c $<
