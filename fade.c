#include "fade.h"
#include "waylogout.h"
#include <stdlib.h>

void fade_update(struct waylogout_fade *fade, uint32_t time) {
	if (fade->current_time >= fade->target_time) {
		return;
	}

	double delta = 0;
	if (fade->old_time != 0) {
		delta = time - fade->old_time;
	}
	fade->old_time = time;

	fade->current_time += delta;
	if (fade->current_time > fade->target_time) {
		fade->current_time = fade->target_time;
	}

	fade->alpha = (double)fade->current_time / (double)fade->target_time;
}

bool fade_is_complete(struct waylogout_fade *fade) {
	return fade->target_time == 0 || fade->current_time >= fade->target_time;
}
