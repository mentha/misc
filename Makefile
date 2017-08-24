LIBC_SO=libc.so.6
CFLAGS=-shared -ldl -fPIC -D"LIBC_SO=\"$(LIBC_SO)\""

TARGETS=inetstrip.so

all: $(TARGETS)

clean:
	+rm -f $(TARGETS)

.SUFFIXES: .c .so

.c.so:
	cc $(CFLAGS) -o $@ $<
