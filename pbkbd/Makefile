KBDPKGS       = libevdev
KBDPKGCFLAGS != pkg-config --cflags $(KBDPKGS)
KBDPKGLDLIBS != pkg-config --libs $(KBDPKGS)
CFLAGS    := -Wall -Os

all: pbkbd pbkbd-backlight

pbkbd: pbkbd.c keymap.h
	$(CC) $(CFLAGS) $(KBDPKGCFLAGS) -o $@ pbkbd.c $(KBDPKGLDLIBS)
