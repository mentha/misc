#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define KBDBL "/sys/class/leds/chromeos::kbd_backlight"
#define IIODEVS "/sys/bus/iio/devices"

#define LIGHT_NAME "cros-ec-light"
#define LIGHT_PROP "in_illuminance_input"

#define AVGPERIOD 10
#define SAMPLERATE 5

//#define DEBUG(...)
#define DEBUG(...) printf(__VA_ARGS__)

#ifndef O_SEARCH /* glibc hack */
# define O_SEARCH 0
#endif

struct lux_mapping_entry {
	double lux;
	double bri;
};

const struct lux_mapping_entry lux_mapping[] = {
	{ 0, 0.01 },
	{ 0.5, 0.3 },
	{ 5, 0.8 },
	{ 10, 1 }
};
const size_t lux_mapping_size = sizeof(lux_mapping) / sizeof(struct lux_mapping_entry);
#define LUX(i) lux_mapping[i].lux
#define BRI(i) lux_mapping[i].bri
const double disable_threshold = 10;
const double reenable_threshold = 5; /* lux must fall below this to reenable lights */

#define ROUND(d) ((long long) ((d) + 0.5))

static double get_bl(double lux)
{
	size_t i;
	for (i = 0; i < lux_mapping_size; i++)
		if (LUX(i) >= lux)
			break;
	if (i >= lux_mapping_size)
		return 0;
	if (LUX(i) == lux)
		return BRI(i);
	double r = (lux - LUX(i - 1)) *
		(BRI(i) - BRI(i - 1)) /
		(LUX(i) - LUX(i - 1)) +
		BRI(i - 1);

	DEBUG("get_bl(%lf) = %lf\n", lux, r);
	return r;
}

static long long readnum(int d, const char *path)
{
	int f = openat(d, path, O_RDONLY);
	if (f < 0)
		return -1;
	char buf[100];
	ssize_t s = read(f, buf, 99);
	buf[s] = 0;
	long long r = strtoll(buf, NULL, 0);
	close(f);
	return r;
}

static int writenum(int d, const char *path, long long n)
{
	int f = openat(d, path, O_WRONLY);
	if (f < 0)
		return -1;
	int r = 0;
	if (dprintf(f, "%lld", n) < 0)
		r = -1;
	close(f);
	return r;
}

static void set_backlight(double v)
{
	int d = open(KBDBL, O_RDONLY | O_SEARCH);
	if (d < 0)
		return;

	long long bri = readnum(d, "max_brightness");
	if (bri < 0)
		goto exit_close_d;
	writenum(d, "brightness", ROUND(v * bri));

exit_close_d:
	close(d);
}

static int check_sensor(int d)
{
	int f = openat(d, "name", O_RDONLY);
	char buf[100];
	ssize_t s = read(f, buf, 99);
	close(f);
	buf[s] = 0;
	char *nl = strchr(buf, '\n');
	if (nl != NULL)
		*nl = 0;
	if (strcmp(buf, LIGHT_NAME))
		return 1;
	if (readnum(d, LIGHT_PROP) >= 0)
		return 0;
	return 1;
}

static int find_sensor(void)
{
	DIR *dd = opendir(IIODEVS);
	if (dd == NULL)
		return -1;

	int ret = -1;

	struct dirent *e;
	for (e = readdir(dd); e != NULL; e = readdir(dd)) {
		if (!(strlen(e->d_name) > 10 &&
					!memcmp(e->d_name, "iio:device", 10)))
			continue;
		int d = openat(dirfd(dd), e->d_name, O_RDONLY | O_SEARCH);
		if (d < 0)
			continue;
		if (!check_sensor(d)) {
			ret = d;
			break;
		}
		close(d);
	}

	closedir(dd);
	return ret;
}

static int keeploop = 1;

static void sighandler(int sig)
{
	keeploop = 0;
}

int main(void)
{
	int sensor = find_sensor();

	if (sensor < 0)
		return 1;

	struct sigaction sa = {
		.sa_handler = sighandler
	};
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	const size_t avgsz = AVGPERIOD * SAMPLERATE;
	int luxbuf[avgsz];
	int bufready = 0;
	int bufidx = 0;
	long long bufsum = 0;

	int waitenable = 0;

	while (keeploop) {
		int lux = readnum(sensor, LIGHT_PROP);
		if (bufready)
			bufsum -= luxbuf[bufidx];
		luxbuf[bufidx] = lux;
		bufsum += lux;
		DEBUG("read raw %d into idx %d ready %d sum %lld avg %lf\n", lux, bufidx, bufready, bufsum, (double) bufsum / avgsz);
		if (bufidx == avgsz - 1) {
			bufidx = 0;
			if (!bufready)
				bufready = 1;
		} else {
			bufidx += 1;
		}
		if (bufready) {
			double lux = (double) bufsum / avgsz;
			if (!waitenable || lux <= reenable_threshold) {
				double bl = 0;
				if (lux >= disable_threshold) {
					waitenable = 1;
				} else {
					waitenable = 0;
					bl = get_bl(lux);
				}
				set_backlight(bl);
			}
		}
		struct timespec ts = {
			.tv_nsec = 1000000000 / SAMPLERATE - 1
		};
		clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL);
	}

	close(sensor);

	return 0;
}
