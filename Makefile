CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
PREFIX ?= /usr/local

CORE_SRCS = protocol/hid.c protocol/transport.c devices/registry.c
CORE_OBJS = $(CORE_SRCS:.c=.o)

CLI_BIN = ajazzyctl
GUI_BIN = ajazzy-gui
GETTEXT_DOMAIN = ajazzy

GUI_CFLAGS := $(shell pkg-config --cflags gtk4 libadwaita-1 2>/dev/null) -DLOCALEDIR=\"$(CURDIR)/locale\" -DICONDIR=\"$(CURDIR)/gui/icons\"
GUI_LIBS := $(shell pkg-config --libs gtk4 libadwaita-1 2>/dev/null)
HAVE_GUI := $(shell pkg-config --exists gtk4 libadwaita-1 2>/dev/null && echo yes)
HAVE_GETTEXT := $(shell which msgfmt xgettext >/dev/null 2>&1 && echo yes)

.PHONY: all clean install uninstall gui locales pot

all: $(CLI_BIN) gui locales

gui:
ifeq ($(HAVE_GUI),yes)
	$(MAKE) $(GUI_BIN)
else
	@echo "gtk4/libadwaita-1 not found via pkg-config, skipping the GUI build."
endif

$(CLI_BIN): cli/main.o $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(GUI_BIN): gui/main.c gui/tray.c gui/tray.h $(CORE_OBJS)
	$(CC) $(CFLAGS) $(GUI_CFLAGS) -o $@ gui/main.c gui/tray.c $(CORE_OBJS) $(GUI_LIBS)

# Regenerates po/ajazzy.pot from the source strings. Not run by
# default -- only needed after adding/changing _() strings in gui/main.c or gui/tray.c.
pot:
	xgettext --from-code=UTF-8 -k_ -kN_ -o po/$(GETTEXT_DOMAIN).pot gui/main.c gui/tray.c
	@echo "now merge new strings into po/*.po with: msgmerge -U po/pt_BR.po po/$(GETTEXT_DOMAIN).pot"

locales:
ifeq ($(HAVE_GETTEXT),yes)
	@mkdir -p locale/pt_BR/LC_MESSAGES
	msgfmt po/pt_BR.po -o locale/pt_BR/LC_MESSAGES/$(GETTEXT_DOMAIN).mo
else
	@echo "gettext tools not found, skipping translation build (GUI will fall back to English)."
endif

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f cli/main.o $(CORE_OBJS) $(CLI_BIN) $(GUI_BIN)
	rm -rf locale

install: all
	install -Dm755 $(CLI_BIN) $(DESTDIR)$(PREFIX)/bin/$(CLI_BIN)
	install -Dm644 udev/71-ajazzy.rules $(DESTDIR)/etc/udev/rules.d/71-ajazzy.rules
ifeq ($(HAVE_GUI),yes)
	install -Dm755 $(GUI_BIN) $(DESTDIR)$(PREFIX)/bin/$(GUI_BIN)
	install -Dm644 locale/pt_BR/LC_MESSAGES/$(GETTEXT_DOMAIN).mo $(DESTDIR)$(PREFIX)/share/locale/pt_BR/LC_MESSAGES/$(GETTEXT_DOMAIN).mo
	install -Dm644 gui/icons/io.github.ajazzy.Gui.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/io.github.ajazzy.Gui.svg
	install -Dm644 gui/io.github.ajazzy.Gui.desktop $(DESTDIR)$(PREFIX)/share/applications/io.github.ajazzy.Gui.desktop
endif

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(CLI_BIN)
	rm -f $(DESTDIR)$(PREFIX)/bin/$(GUI_BIN)
	rm -f $(DESTDIR)/etc/udev/rules.d/71-ajazzy.rules
	rm -f $(DESTDIR)$(PREFIX)/share/locale/pt_BR/LC_MESSAGES/$(GETTEXT_DOMAIN).mo
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/io.github.ajazzy.Gui.svg
	rm -f $(DESTDIR)$(PREFIX)/share/applications/io.github.ajazzy.Gui.desktop
