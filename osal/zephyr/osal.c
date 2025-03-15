#include <zephyr/posix/sys/time.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>

#include "osal.h"

int osal_usleep(uint32 usec)
{
	k_usleep(usec);

	return 0;
}

ec_timet osal_current_time(void)
{
	struct timespec current_time;
	ec_timet return_value;

	clock_gettime(CLOCK_REALTIME, &current_time);
	return_value.sec = current_time.tv_sec;
	return_value.usec = current_time.tv_nsec / 1000;

	return return_value;
}

void osal_time_diff(ec_timet *start, ec_timet *end, ec_timet *diff)
{
	if (end->usec < start->usec) {
		diff->sec = end->sec - start->sec - 1;
		diff->usec = end->usec + 1000000 - start->usec;
	} else {
		diff->sec = end->sec - start->sec;
		diff->usec = end->usec - start->usec;
	}
}

/* Returns time from some unspecified moment in past,
 * strictly increasing, used for time intervals measurement. */
static void osal_getrelativetime(struct timeval *tv)
{
	struct timespec ts;

	/* Use clock_gettime to prevent possible live-lock.
	 * Gettimeofday uses CLOCK_REALTIME that can get NTP timeadjust.
	 * If this function preempts timeadjust and it uses vpage it live-locks.
	 * Also when using XENOMAI, only clock_gettime is RT safe */
	clock_gettime(CLOCK_MONOTONIC, &ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
}

#define  timeradd(a, b, result)                             \
  do {                                                      \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;           \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;        \
    if ((result)->tv_usec >= 1000000)                       \
    {                                                       \
       ++(result)->tv_sec;                                  \
       (result)->tv_usec -= 1000000;                        \
    }                                                       \
  } while (0)

void osal_timer_start(osal_timert *self, uint32 timeout_usec)
{
	struct timeval start_time;
	struct timeval timeout;
	struct timeval stop_time;

	osal_getrelativetime(&start_time);
	timeout.tv_sec = timeout_usec / USEC_PER_SEC;
	timeout.tv_usec = timeout_usec % USEC_PER_SEC;
	timeradd(&start_time, &timeout, &stop_time);

	self->stop_time.sec = stop_time.tv_sec;
	self->stop_time.usec = stop_time.tv_usec;
}

#define  timercmp(a, b, CMP)                                \
  (((a)->tv_sec == (b)->tv_sec) ?                           \
   ((a)->tv_usec CMP (b)->tv_usec) :                        \
   ((a)->tv_sec CMP (b)->tv_sec))

boolean osal_timer_is_expired(osal_timert *self)
{
	struct timeval current_time;
	struct timeval stop_time;
	int is_not_yet_expired;

	osal_getrelativetime(&current_time);
	stop_time.tv_sec = self->stop_time.sec;
	stop_time.tv_usec = self->stop_time.usec;
	is_not_yet_expired = timercmp(&current_time, &stop_time, <);

	return is_not_yet_expired == FALSE;
}
