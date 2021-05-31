#ifndef _WAYLOGOUT_FADE_H
#define _WAYLOGOUT_FADE_H

#include <stdbool.h>
#include <stdint.h>

struct pool_buffer;

struct waylogout_fade {
	float current_time;
	float target_time;
	uint32_t old_time;
	uint32_t *original_buffer;
};

void fade_prepare(struct waylogout_fade *fade, struct pool_buffer *buffer);
void fade_update(struct waylogout_fade *fade, struct pool_buffer *buffer, uint32_t time);
bool fade_is_complete(struct waylogout_fade *fade);
void fade_destroy(struct waylogout_fade *fade);

#endif
