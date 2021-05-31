#ifndef _SWAYLOGOUT_FADE_H
#define _SWAYLOGOUT_FADE_H

#include <stdbool.h>
#include <stdint.h>

struct pool_buffer;

struct swaylogout_fade {
	float current_time;
	float target_time;
	uint32_t old_time;
	uint32_t *original_buffer;
};

void fade_prepare(struct swaylogout_fade *fade, struct pool_buffer *buffer);
void fade_update(struct swaylogout_fade *fade, struct pool_buffer *buffer, uint32_t time);
bool fade_is_complete(struct swaylogout_fade *fade);
void fade_destroy(struct swaylogout_fade *fade);

#endif
