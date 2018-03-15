/*
 * p1bench - perturbation benchmark. Tests simple CPU or memory loops.
 *
 * This is intended to be used before other benchmark tests, to investigate
 * CPU and memory variation. This helps you better interpret the results of any
 * microbenchmark test, by characterizing variance that will sway results.
 *
 * Let's say you wanted to do a 500 ms CPU benchmark of gzip performance:
 * p1bench can be run with a 500 ms interval to show what baseline variation
 * you may see, based on a simple spin loop, before running the more complex
 * gzip microbenchmark.
 *
 * p1bench can also be run in a mode (-m) to test memory variation.
 *
 * gcc -O0 -pthread -o p1bench p1bench.c
 *
 * USAGE: see -h for usage.
 *
 * Copyright 2018 Netflix, Inc.
 * Licensed under the Apache License, Version 2.0 (the "License")
 *
 * 03-Jan-2018	Brendan Gregg	Created this.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

void usage()
{
	printf("USAGE: p1bench [-hv] [-m Mbytes] [time(ms) [count]]\n"
	    "                   -v         # verbose: per run details\n"
	    "                   -m Mbytes  # memory test working set\n"
	    "   eg,\n"
	    "       p1bench          # 100ms (default) CPU spin loop\n"
	    "       p1bench 300      # 300ms CPU spin loop\n"
	    "       p1bench 300 100  # 300ms CPU spin loop, 100 times\n"
	    "       p1bench -m 1024  # 1GB memory read loop\n");
}

/*
 * These functions aren't just for code cleanliness: they show up in profilers
 * when doing active benchmarking to debug the benchmark.
 */

int g_testrun = 1;
void teststop(int dummy) {
	g_testrun = 0;
}

void *spintest(void *arg)
{
	signal(SIGUSR1, teststop);
	unsigned long long *count = (unsigned long long *)arg;
	for (;g_testrun;) { (*count)++; }
	return NULL;
}

unsigned long long spinrun(unsigned long long count)
{
	unsigned long long i;
	// check compiler doesn't elide this (-O0)
	for (i = 0; i < count; i++) {;}
	return i;
}

// memory parameters
char *g_mem;
unsigned long long g_memsize;
unsigned long long g_stride;

void *memtest(void *arg)
{
	unsigned long long *count = (unsigned long long *)arg;
	char *memp;
	unsigned long long i, j;
	int junk;

	signal(SIGUSR1, teststop);
	memp = g_mem;
	for (;g_testrun;) {
		junk += memp[0];
		memp += g_stride;
		if (memp > (g_mem + g_memsize))
			memp = g_mem;
		(*count)++;
	}

	return NULL;
}

unsigned long long memrun(unsigned long long count)
{
	char *memp;
	unsigned long long i, j;
	int junk;

	signal(SIGUSR1, teststop);
	memp = g_mem;
	for (i = 0; i < count; i++) {
		junk += memp[0];
		memp += g_stride;
		if (memp > (g_mem + g_memsize))
			memp = g_mem;
	}
	return i;
}

/*
 * Runs the loop function for the target_us while incrementing count.
 * This gives us a ballpark figure of the target count.
 */
void test_run(unsigned long long target_us, unsigned long long *count,
    void *(*loop)(void *))
{
	pthread_t thread;
	int err;

	if (!target_us)
		return;

	g_testrun = 1;
	(*count) = 0;
	if (pthread_create(&thread, NULL, loop, count) != 0) {
		perror("Thread create failed");
		exit(1);
	}
	usleep(target_us);
	if ((err = pthread_kill(thread, SIGUSR1))) {
		perror("Couldn't terminate worker thread normally");
		exit(1);
	}
	pthread_join(thread, NULL);
}

/*
 * Finds a ballpark target count, then runs the real run function with that
 * count several times (test_runs) to fine tune the target count.
 */
unsigned long long find_count(unsigned long long target_us,
    int test_us, int test_runs,
    void *(*test)(void *),
    unsigned long long (*run)(unsigned long long))
{
	unsigned long long time_us;
	unsigned long long fastest_time_us = ~0ULL;
	unsigned long long iter_count = 0;
	static struct timeval ts[2];
	int i;

	test_run(test_us, &iter_count, test);
	for (i = 0; i < test_runs; i++) {
		gettimeofday(&ts[0], NULL);
		(void) run(iter_count);
		gettimeofday(&ts[1], NULL);
		time_us = 1000000 * (ts[1].tv_sec - ts[0].tv_sec) +
		    (ts[1].tv_usec - ts[0].tv_usec) / 1;
		if (time_us < fastest_time_us)
			fastest_time_us = time_us;
	}
	return iter_count * target_us / fastest_time_us;
}

/*
 * Value to histogram index.
 * This is a custom histogram with the following ranges:
 *     value 0-1: 0.1 step size, idx 0-9
 *     value 1-20: 1 step size, idx 10-28
 *     value 20+: 10 step size, idx 19+
 */
static int hist_idx(double value, int buckets)
{
	int idx;

	if (value < 1)
		idx = (int)(10 * value);
	else if (value < 20)
		// idx = previous_steps + (value - min_range_value)
		idx = 10 + (int)(value - 1);
	else
		// idx = previous_steps + (value - min_range_value)/10
		idx = 29 + (int)((value - 20) / 10);

	if (idx > buckets - 1)
		idx = buckets - 1;
	return idx;
}

// histogram index to minimum value
double hist_val(int idx)
{
	if (idx < 10)
		return (double)idx / 10;
	else if (idx < 29)
		// 10 -> 1.0
		// 11 -> 2
		// value = idx - previous_steps + min_range_value
		return (double)idx - 10 + 1;
	else
		// value = (idx - previous_steps) * 10 + min_range_value
		return (double)(idx - 29) * 10 + 20;
}

static int ullcmp(const void *p1, const void *p2)
{
	unsigned long long a = *(unsigned long long *)p1;
	unsigned long long b = *(unsigned long long *)p2;
	return a - b;
}

// not worth -lm for this
int myceil(double x)
{
	if (x > (int)x)
		return (int)x + 1;
	return (int)x;
}

int g_mainrun = 1;
void mainstop(int dummy) {
	g_mainrun = 0;
	printf("stopping...\n");
}

// histogram bucket count
#define BUCKETS	200

int main(int argc, char *argv[])
{
	unsigned long long iter_count, time_us, time_usr_us,
	    time_sys_us, ivcs, last_us, total_time_us, fastest_time_us,
	    slowest_time_us;
	unsigned long long target_us = 100 * 1000;	// default target ms
	double slower_ms, diff_pct;
	static struct timeval ts[2];
	struct rusage u[2];
	int test_us = 100 * 1000;
	int test_runs = 5;	// calibration
	int max_runs = 100;
	int verbose = 0;
	int hist[BUCKETS] = {0};
	int bar_width = 50;
	int c, i, j, runs, idx, max_idx;
	unsigned long long *runs_us;
	unsigned long long pagesize;
	char *memp;
	unsigned long long (*run)(unsigned long long) = spinrun;
	void *(*test)(void *) = spintest;

	// defaults
	g_stride = 64;
	g_memsize = 0;

	// options
	while ((c = getopt(argc, argv, "hm:v")) != -1) {
		switch (c) {
		case 'm':
			g_memsize = atoi(optarg) * 1024 * 1024;
			if (!g_memsize) {
				printf("-m Mbytes must be non-zero\n");
				usage();
				return 0;
			}
			run = memrun;
			test = memtest;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
			usage();
			return 0;
		default:
			usage();
			return 0;
		}
	}
	argc -= optind;
	if (argc > 2) {
		usage();
		return 0;
	}
	if (argc)
		target_us = atoll(argv[optind]) * 1000;
	if (argc > 1)
		max_runs = atoll(argv[optind + 1]);
	if (!target_us) {
		printf("ERROR: target ms must be > 0\n");
		usage();
		return 1;
	}

	// per-run statistics
	if ((runs_us = malloc(max_runs * sizeof (time_us))) == NULL) {
		printf("ERROR: can't allocate memory for %d runs\n", max_runs);
		return 1;
	}

	/*
	 * populate working set
	 */
	if (g_memsize) {
		printf("Allocating %llu Mbytes...\n",
		    g_memsize / (1024 * 1024));
		if ((g_mem = malloc(g_memsize)) == NULL) {
			printf("ERROR allocating -m memory. Exiting.\n");
			return 1;
		}
		pagesize = getpagesize();
		for (memp = g_mem; memp < (g_mem + g_memsize);
		    memp += pagesize) {
			memp[0] = 'A';
		}
	}

	/*
	 * determine target run count
	 */
	printf("Calibrating for %llu ms...", target_us / 1000);
	fflush(stdout);
	iter_count = find_count(target_us, test_us, test_runs, test, run);
	printf(" (target iteration count: %llu)\n", iter_count);

	signal(SIGINT, mainstop);
	time_us = 0;
	diff_pct = 0;

	// run loop
	fastest_time_us = ~0ULL;
	slowest_time_us = 0;
	for (i = 0; g_mainrun && i < max_runs; i++) {
		last_us = time_us;
		/*
		 * spin time, with timeout
		 */
		getrusage(RUSAGE_SELF, &u[0]);
		gettimeofday(&ts[0], NULL);
		(void) run(iter_count);
		gettimeofday(&ts[1], NULL);
		getrusage(RUSAGE_SELF, &u[1]);

		/*
		 * calculate times
		 */
		time_us = 1000000 * (ts[1].tv_sec - ts[0].tv_sec) +
		    (ts[1].tv_usec - ts[0].tv_usec) / 1;
		if (time_us < fastest_time_us)
			fastest_time_us = time_us;
		if (time_us > slowest_time_us)
			slowest_time_us = time_us;
		runs_us[i] = time_us;
		if (last_us)
			diff_pct = 100 * (((double)time_us / last_us) - 1);

		// status output
		if (!verbose) {
			printf("\rRun %d/%d, Ctrl-C to stop (%.2f%% diff)  ",
			    i + 1, max_runs, diff_pct);
			fflush(stdout);
			continue;
		}

		// debug stats
		time_usr_us = 1000000 *
		    (u[1].ru_utime.tv_sec - u[0].ru_utime.tv_sec) +
		    (u[1].ru_utime.tv_usec - u[0].ru_utime.tv_usec) / 1;
		time_sys_us = 1000000 *
		    (u[1].ru_stime.tv_sec - u[0].ru_stime.tv_sec) +
		    (u[1].ru_stime.tv_usec - u[0].ru_stime.tv_usec) / 1;
		ivcs = u[1].ru_nivcsw - u[0].ru_nivcsw;

		// verbose output
		if (i == 0) {
			printf("%s %s %s %s %s %s\n", "run", "time(ms)",
			    "usr_time(ms)", "sys_time(ms)",
			    "involuntary_csw", "diff%");
			printf("%d %.2f %.1f %.1f %llu -\n", i + 1,
			    (double)time_us / 1000,
			    (double)time_usr_us / 1000,
			    (double)time_sys_us / 1000, ivcs);
		} else {
			printf("%d %.2f %.1f %.1f %llu %.1f\n", i + 1,
			    (double)time_us / 1000,
			    (double)time_usr_us / 1000,
			    (double)time_sys_us / 1000, ivcs, diff_pct);
		}
	}
	runs = i;

	/*
	 * post-process: histogram and percentiles
	 */
	total_time_us = 0;
	max_idx = 0;
	for (i = 0; i < runs; i++) {
		idx = hist_idx(100 *
		    (((double)runs_us[i] / fastest_time_us) - 1), BUCKETS);
		if (idx < 0) {
			// shouldn't happen
			printf("ERROR: negative hist idx; fix program.\n");
			return 1;
		}
		hist[idx]++;
		if (idx > max_idx)
			max_idx = idx;
		total_time_us += runs_us[i];
	}
	int max_bucket_count = 0;
	for (i = 0; i <= max_idx; i++) {
		if (hist[i] > max_bucket_count)
			max_bucket_count = hist[i];
	}

	/*
	 * print histogram and stats
	 */
	if (!verbose)
		printf("\n");
	printf("\nPerturbation percent by count for %llu ms runs:\n",
	    target_us / 1000);
	printf("%9s  %6s %7s %s\n", "Slower%", "Count", "Count%", "Histogram");
	int bar;
	double min;
	for (i = 0; i <= max_idx; i++) {
		min = hist_val(i);
		printf("%8.1f%%%s %6d %6.2f%% ", min,
		    i == BUCKETS - 1 ? "+" : ":", hist[i],
		    (double)100 * hist[i] / runs);
		bar = myceil((double)bar_width * hist[i] / max_bucket_count);
		for (j = 0; j < bar; j++)
			printf("*");
		printf("\n");
	}

	qsort(runs_us, runs, sizeof (time_us), ullcmp);
	printf("\nPercentiles:");
	if (runs >= 3) {
		printf(" 50th: %.3f%%", (double)100 *
		    (runs_us[runs * 50 / 100 - 1] - fastest_time_us) /
		    fastest_time_us);
	}
	if (runs >= 10) {
		printf(", 90th: %.3f%%", (double)100 *
		    (runs_us[runs * 90 / 100 - 1] - fastest_time_us) /
		    fastest_time_us);
	}
	if (runs >= 100) {
		printf(", 99th: %.3f%%", (double)100 *
		    (runs_us[runs * 99 / 100 - 1] - fastest_time_us) /
		    fastest_time_us);
	}
	if (runs >= 3)
		printf(",");
	printf(" 100th: %.3f%%\n", (double)100 * 
	    (runs_us[runs - 1] - fastest_time_us) / fastest_time_us);

	printf("Fastest: %.3f ms, 50th: %.3f ms, mean: %.3f ms, "
	    "slowest: %.3f ms\n",
	    (double)fastest_time_us / 1000,
	    (double)runs_us[runs * 50 / 100 - 1] / 1000,
	    (double)total_time_us / (runs * 1000),
	    (double)slowest_time_us / 1000);
	printf("Fastest rate: %llu/s, 50th: %llu/s, mean: %llu/s, "
	    "slowest: %llu/s\n",
	    iter_count * 1000000 / runs_us[0],
	    iter_count * 1000000 / runs_us[runs * 50 / 100 - 1],
	    iter_count * 1000000 / (total_time_us / runs),
	    iter_count * 1000000 / runs_us[runs - 1]);
  
	return (0);
}
