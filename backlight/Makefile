PKGS       = fuse
PKGCFLAGS != pkg-config --cflags $(PKGS)
PKGLDLIBS != pkg-config --libs $(PKGS)
CFLAGS    := $(PKGCFLAGS) -Wall -Os -DNDEBUG
LDLIBS    := $(PKGLDLIBS)

all: smoothbacklightfs aliasbacklightfs
