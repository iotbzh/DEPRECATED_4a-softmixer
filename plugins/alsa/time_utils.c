#include "time_utils.h"
#include "time.h"

__thread uint64_t last = 0;

uint64_t now_monotonic_usec() {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec*1000000+now.tv_nsec/1000;
}

uint64_t ts() {
	uint64_t now = now_monotonic_usec();
	uint64_t elapsed = now-last;
	last = now;
	return elapsed;
}
