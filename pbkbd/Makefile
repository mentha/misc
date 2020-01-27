PKGS       = libevdev
PKGCFLAGS != pkg-config --cflags $(PKGS)
PKGLDLIBS != pkg-config --libs $(PKGS)
CFLAGS    := $(PKGCFLAGS) -Wall -Os
LDLIBS    := $(PKGLDLIBS)

all: pbkbd

pbkbd: pbkbd.c keymap.h
	$(CC) $(CFLAGS) -o $@ pbkbd.c $(LDLIBS)
