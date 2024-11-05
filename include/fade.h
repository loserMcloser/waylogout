#ifndef _WAYLOGOUT_FADE_H
#define _WAYLOGOUT_FADE_H

#include <stdbool.h>
#include <stdint.h>

struct waylogout_fade {
	float current_time;
	float target_time;
	uint32_t old_time;
	double alpha;
};

void fade_update(struct waylogout_fade *fade, uint32_t time);
bool fade_is_complete(struct waylogout_fade *fade);

#endif
