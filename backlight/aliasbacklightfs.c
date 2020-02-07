#define _POSIX_C_SOURCE 200809L
#define FUSE_USE_VERSION 26

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_SEARCH
# define O_SEARCH 0
#endif

ssize_t readfile(int dfd, const char *path, void *buf, size_t bufsz)
{
	int fd = openat(dfd, path, O_RDONLY | O_NOCTTY);
	if (fd < 0)
		return -1;
	int r = read(fd, buf, bufsz);
	int e = errno;
	close(fd);
	errno = e;
	return r;
}

int readnum(int dfd, const char *path)
{
	char buf[20];
	ssize_t r = readfile(dfd, path, buf, 19);
	if (r < 0)
		return -1;
	buf[r] = 0;
	int n;
	sscanf(buf, "%d", &n);
	if (n < 0)
		n = 0;
	return n;
}

#ifndef NDEBUG
# define ENABLE_TRACE 1
#else
# define ENABLE_TRACE 0
#endif

static int request_bri_update(int tfd, int bri, int maxbri)
{
	int tmax = readnum(tfd, "max_brightness");
	if (tmax <= 0)
		return 1;
	int tbri = (long long) tmax * bri / maxbri;
	char buf[20];
	sprintf(buf, "%d\n", tbri);

	int fd = openat(tfd, "brightness", O_WRONLY | O_NOCTTY);
	if (fd < 0)
		return 1;
	write(fd, buf, strlen(buf));
	close(fd);

	return 0;
}

static int tfd;
static int dfd;

static const char *pathfix(const char *p)
{
	if (p[0] == '/')
		p++;
	if (p[0] == 0 || !strcmp(p, ".."))
		return ".";
	return p;
}

static int fs_readlink(const char *path, char *buf, size_t bufsz)
{
	ssize_t r = readlinkat(dfd, pathfix(path), buf, bufsz - 1);
	if (r < 0)
		return -errno;
	buf[r] = 0;
	return 0;
}

static int trace_readlink(const char *path, char *buf, size_t bufsz)
{
	if (ENABLE_TRACE)
		printf("readlink(\"%s\", ..., %zu)", path, bufsz);
	int r = fs_readlink(path, buf, bufsz);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => 0, \"%s\"\n", buf);
	}
	return r;
}

static int fs_getattr(const char *path, struct stat *st)
{
	int r = fstatat(dfd, pathfix(path), st, AT_SYMLINK_NOFOLLOW);
	if (r)
		return -errno;
	st->st_size = 4096;
	return 0;
}

static int trace_getattr(const char *path, struct stat *st)
{
	if (ENABLE_TRACE)
		printf("getattr(\"%s\")", path);
	int r = fs_getattr(path, st);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => 0\n");
	}
	return r;
}

static int fs_truncate(const char *path, off_t sz)
{
	path = pathfix(path);
	if (!strcmp(path, "brightness") || !strcmp(path, "actual_brightness"))
		return 0;
	if (truncate(pathfix(path), sz))
		return -errno;
	return 0;
}

static int trace_truncate(const char *path, off_t sz)
{
	if (ENABLE_TRACE)
		printf("truncate(\"%s\", %zu)", path, sz);
	int r = fs_truncate(path, sz);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => 0\n");
	}
	return r;
}

static int fs_opendir(const char *path, struct fuse_file_info *fi)
{
	static_assert(sizeof(uintptr_t) <= sizeof(fi->fh), "fi->fh too small");
	int fd = openat(dfd, pathfix(path), O_RDONLY | O_SEARCH | O_DIRECTORY |
			O_NOFOLLOW | O_NOCTTY);
	if (fd < 0)
		return -errno;
	DIR *d = fdopendir(fd);
	if (d == NULL) {
		int e = errno;
		close(fd);
		return -e;
	}
	fi->fh = (uintptr_t) d;
	return 0;
}

static int trace_opendir(const char *path, struct fuse_file_info *fi)
{
	if (ENABLE_TRACE)
		printf("opendir(\"%s\")", path);
	int r = fs_opendir(path, fi);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => 0, %p\n", (void *) fi->fh);
	}
	return r;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filldir,
		off_t ignored_, struct fuse_file_info *fi)
{
	DIR *d = (DIR *) fi->fh;
	struct dirent *e;
	for (e = readdir(d); e != NULL; e = readdir(d)) {
		filldir(buf, e->d_name, NULL, 0);
	}
	return 0;
}

static int trace_readdir(const char *path, void *buf, fuse_fill_dir_t filldir,
		off_t ignored_, struct fuse_file_info *fi)
{
	if (ENABLE_TRACE)
		printf("readdir(\"%s\", %p)", path, (void *) fi->fh);
	int r = fs_readdir(path, buf, filldir, ignored_, fi);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => 0\n");
	}
	return r;
}

static int fs_releasedir(const char *path, struct fuse_file_info *fi)
{
	DIR *d = (DIR *) fi->fh;
	if (closedir(d))
		return -errno;
	return 0;
}

static int trace_releasedir(const char *path, struct fuse_file_info *fi)
{
	if (ENABLE_TRACE)
		printf("readdir(\"%s\", %p)", path, (void *) fi->fh);
	int r = fs_releasedir(path, fi);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => 0\n");
	}
	return r;
}

static int fs_open(const char *path, struct fuse_file_info *fi)
{
	path = pathfix(path);
	if (!strcmp(path, "brightness")) {
		return 0;
	} else if (!strcmp(path, "actual_brightness")) {
		if (O_WRONLY && (fi->flags & O_WRONLY))
			return -EACCES;
		return 0;
	}
	int fd = openat(dfd, path, (fi->flags | O_NOFOLLOW | O_NOCTTY) &
			~(O_CREAT | O_NONBLOCK));
	if (fd < 0)
		return -errno;
	fi->fh = fd;
	return 0;
}

static int trace_open(const char *path, struct fuse_file_info *fi)
{
	if (ENABLE_TRACE)
		printf("open(\"%s\", 0x%x)", path, fi->flags);
	int r = fs_open(path, fi);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => %d\n", (int) fi->fh);
	}
	return r;
}

static int target_brightness = 0;
static int max_brightness = 0;

static int fs_read(const char *path, char *buf, size_t sz, off_t off,
		struct fuse_file_info *fi)
{
	path = pathfix(path);
	if (!strcmp(path, "brightness")) {
		snprintf(buf, sz, "%d\n", target_brightness);
		return strlen(buf);
	} else if (!strcmp(path, "actual_brightness")) {
		int rb = readnum(dfd, "brightness");
		snprintf(buf, sz, "%d\n", rb);
		return strlen(buf);
	} else {
		ssize_t r = pread(fi->fh, buf, sz, off);
		if (r < 0)
			return -errno;
		return r;
	}
}

static int trace_read(const char *path, char *buf, size_t sz, off_t off,
		struct fuse_file_info *fi)
{
	if (ENABLE_TRACE)
		printf("pread(%d \"%s\", ..., %zu, %zu)", (int) fi->fh, path, sz, off);
	int r = fs_read(path, buf, sz, off, fi);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => %d\n", r);
	}
	return r;
}

static int fs_write(const char *path, const char *buf, size_t sz, off_t off,
		struct fuse_file_info *fi)
{
	path = pathfix(path);
	if (!strcmp(path, "brightness")) {
		char nbuf[sz + 1];
		memcpy(nbuf, buf, sz);
		nbuf[sz] = 0;
		int b;
		if (sscanf(nbuf, "%d", &b) != 1)
			return -EINVAL;
		if (b < 0)
			b = 0;
		if (b > max_brightness)
			b = max_brightness;
		target_brightness = b;
		if (request_bri_update(tfd, b, max_brightness)) {
			perror("cannot request brightness update");
			kill(getpid(), SIGINT);
		}
		return sz;
	} else if (!strcmp(path, "actual_brightness")) {
		return -EBADF;
	} else {
		ssize_t r = pwrite(fi->fh, buf, sz, off);
		if (r < 0)
			return -errno;
		return r;
	}
}

static int trace_write(const char *path, const char *buf, size_t sz, off_t off,
		struct fuse_file_info *fi)
{
	if (ENABLE_TRACE)
		printf("pwrite(%d \"%s\", ..., %zu, %zu)", (int) fi->fh, path, sz, off);
	int r = fs_write(path, buf, sz, off, fi);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => %d\n", r);
	}
	return r;
}

static int fs_release(const char *path, struct fuse_file_info *fi)
{
	path = pathfix(path);
	if (!strcmp(path, "brightness") ||
			!strcmp(path, "actual_brightness"))
		return 0;
	if (close(fi->fh))
		return -errno;
	return 0;
}

static int trace_release(const char *path, struct fuse_file_info *fi)
{
	if (ENABLE_TRACE)
		printf("close(%d \"%s\")", (int) fi->fh, path);
	int r = fs_release(path, fi);
	if (ENABLE_TRACE) {
		if (r < 0)
			printf(" => error %d: %s\n", r, strerror(-r));
		else
			printf(" => %d\n", r);
	}
	return r;
}

#if ENABLE_TRACE
# define TRACE_UNIMPLEMENTED(n) ((void *) trace_unimplemented_##n)
# define DEFINE_TRACE_UNIMPLEMENTED(n) \
	static int trace_unimplemented_##n(void) \
	{ \
		puts(#n "(...) not implemented"); \
		return -ENOSYS; \
	}
#else
# define TRACE_UNIMPLEMENTED(n) NULL
# define DEFINE_TRACE_UNIMPLEMENTED(n)
#endif

/* DEFINE_TRACE_UNIMPLEMENTED(truncate); */

struct fuse_operations fsops = {
	.readlink = trace_readlink,
	.getattr = trace_getattr,
	.truncate = trace_truncate,

	.opendir = trace_opendir,
	.readdir = trace_readdir,
	.releasedir = trace_releasedir,

	.open = trace_open,
	.read = trace_read,
	.write = trace_write,
	.release = trace_release,
};

int main(int argc, char **argv)
{
	if (argc != 3 || argv[1][0] == '-' || argv[2][0] == '-') {
		fprintf(stderr, "usage: %s <dir> <target>\n",
				argc == 1 ? argv[0] : "aliasbacklightfs");
		return 1;
	}
	dfd = open(argv[1], O_RDONLY | O_SEARCH | O_DIRECTORY | O_NOCTTY);
	if (dfd < 0) {
		perror("cannot open target directory");
		return 1;
	}
	target_brightness = readnum(dfd, "brightness");
	max_brightness = readnum(dfd, "max_brightness");
	if (target_brightness < 0 ||  max_brightness < 0) {
		fprintf(stderr, "does not look like backlight control\n");
		return 1;
	}
	tfd = open(argv[2], O_RDONLY | O_SEARCH | O_DIRECTORY | O_NOCTTY);
	if (tfd < 0) {
		close(dfd);
		return 1;
	}
	char argv1[] = "-o";
	char argv2[] = "default_permissions,auto_unmount,nonempty,allow_other,"
		"nodev,noexec,nosuid";
	char argv3[] = "-f";
	char argv4[] = "-s";
	char *fuseargv[] = {
		argv[0],
		argv1,
		argv2,
		argv3,
		argv4,
		argv[1]
	};
	int r = fuse_main(sizeof(fuseargv) / sizeof(fuseargv[0]), fuseargv,
			&fsops, NULL);
	close(tfd);
	close(dfd);
	return r;
}
