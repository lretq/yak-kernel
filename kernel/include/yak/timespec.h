#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <yak/types.h>

struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

#ifdef __cplusplus
}
#endif
