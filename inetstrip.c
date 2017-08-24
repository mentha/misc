#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>

#define LOG(...) fprintf(stderr, "inetstrip: " __VA_ARGS__)
#ifndef LIBC_SO
#define LIBC_SO "libc.so.6"
#endif
#define AF_DEFAULT_STRIP AF_INET

typedef int (*socket_t) (int, int, int);
typedef int (*getaddrinfo_t) (const char *, const char *,
		const struct addrinfo *, struct addrinfo **);
typedef void (*freeaddrinfo_t) (struct addrinfo *);

static socket_t c_socket = NULL;
static getaddrinfo_t c_getaddrinfo = NULL;
static freeaddrinfo_t c_freeaddrinfo = NULL;

static inline int
is_ready(void)
{
	return c_socket && c_getaddrinfo && c_freeaddrinfo;
}

static int
loadlib(void)
{
	if (is_ready())
		return 0;
	void *dl;
	dlerror();
	dl = dlopen(LIBC_SO, RTLD_NOW);
	if (dl == NULL) {
		LOG("dlopen failed: %s\n", dlerror());
		return 1;
	};
	c_socket = dlsym(dl, "socket");
	c_getaddrinfo = dlsym(dl, "getaddrinfo");
	c_freeaddrinfo = dlsym(dl, "freeaddrinfo");
	if (!is_ready()) {
		LOG("dlsym failed: %s\n", dlerror());
		c_socket = NULL;
		c_getaddrinfo = NULL;
		c_freeaddrinfo = NULL;
		dlclose(dl);
		return 1;
	};
	return 0;
}

static int
get_strip_family(void)
{
	static int cached_res = AF_DEFAULT_STRIP, cached_valid = 0;
	if (cached_valid)
		return cached_res;
	cached_valid = 1;
	char *s = getenv("STRIPFAMILY");
	if (s == NULL) {
		LOG("warning: environment variable STRIPFAMILY unset\n");
		return AF_DEFAULT_STRIP;
	};
	if (!strcmp("4", s)) {
		cached_res = AF_INET;
		return cached_res;
	} else if (!strcmp("6", s)) {
		cached_res = AF_INET6;
		return cached_res;
	};
	LOG("warning: environment variable STRIPFAMILY='%s' invalid\n", s);
	return AF_DEFAULT_STRIP;
}

extern int
socket(int domain, int type, int protocol)
{
	if (domain == get_strip_family()) {
		errno = EAFNOSUPPORT;
		return -1;
	};
	if (loadlib()) {
		errno = ENOBUFS;
		return -1;
	};
	return c_socket(domain, type, protocol);
}

static void
copyai(struct addrinfo *d, struct addrinfo *s)
{
	d->ai_flags = s->ai_flags;
	d->ai_family = s->ai_family;
	d->ai_socktype = s->ai_socktype;
	d->ai_protocol = s->ai_protocol;
	d->ai_addrlen = s->ai_addrlen;
	d->ai_addr = malloc(d->ai_addrlen);
	memcpy(d->ai_addr, s->ai_addr, d->ai_addrlen);
	if (s->ai_canonname) {
		size_t l = strlen(s->ai_canonname);
		d->ai_canonname = malloc(l + 1);
		memcpy(d->ai_canonname, s->ai_canonname, l + 1);
	} else {
		d->ai_canonname = NULL;
	};
}

static struct addrinfo *
buildaddrinfo(struct addrinfo *res)
{
	struct addrinfo *retval = NULL, *p = NULL;
	for (; res != NULL; res = res->ai_next) {
		if (res->ai_family == get_strip_family())
			continue;
		if (p == NULL) {
			p = malloc(sizeof(struct addrinfo));
			retval = p;
		} else {
			p->ai_next = malloc(sizeof(struct addrinfo));
			p = p->ai_next;
		};
		copyai(p, res);
		p->ai_next = NULL;
	};
	return retval;
}

extern int
getaddrinfo(const char *nodename, const char *servname,
		const struct addrinfo *hints, struct addrinfo **res)
{
	int r;
	struct addrinfo *result, *retval;
	if (loadlib())
		return EAI_MEMORY;
	if ((r = c_getaddrinfo(nodename, servname, hints, &result)) != 0)
		return r;
	retval = buildaddrinfo(result);
	c_freeaddrinfo(result);
	*res = retval;
	return 0;
}

extern void
freeaddrinfo(struct addrinfo *ai)
{
	if (ai->ai_addr)
		free(ai->ai_addr);
	if (ai->ai_canonname)
		free(ai->ai_canonname);
	if (ai->ai_next)
		freeaddrinfo(ai->ai_next);
	free(ai);
}
