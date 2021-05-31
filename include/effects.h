#ifndef _SWAYLOGOUT_EFFECTS_H
#define _SWAYLOGOUT_EFFECTS_H

#include <stdbool.h>

#include "cairo.h"

struct swaylogout_effect_screen_pos {
	float pos;
	bool is_percent;
};

struct swaylogout_effect {
	union {
		struct {
			int radius, times;
		} blur;
		struct {
			int factor;
		} pixelate;
		double scale;
		struct {
			double base;
			double factor;
		} vignette;
		struct {
			struct swaylogout_effect_screen_pos x;
			struct swaylogout_effect_screen_pos y;
			struct swaylogout_effect_screen_pos w;
			struct swaylogout_effect_screen_pos h;
			enum {
				EFFECT_COMPOSE_GRAV_CENTER,
				EFFECT_COMPOSE_GRAV_NW,
				EFFECT_COMPOSE_GRAV_NE,
				EFFECT_COMPOSE_GRAV_SW,
				EFFECT_COMPOSE_GRAV_SE,
				EFFECT_COMPOSE_GRAV_N,
				EFFECT_COMPOSE_GRAV_S,
				EFFECT_COMPOSE_GRAV_E,
				EFFECT_COMPOSE_GRAV_W,
			} gravity;
			char *imgpath;
		} compose;
		char *custom;
	} e;

	enum {
		EFFECT_BLUR,
		EFFECT_PIXELATE,
		EFFECT_SCALE,
		EFFECT_GREYSCALE,
		EFFECT_VIGNETTE,
		EFFECT_COMPOSE,
		EFFECT_CUSTOM,
	} tag;
};

cairo_surface_t *swaylogout_effects_run(cairo_surface_t *surface, int scale,
		struct swaylogout_effect *effects, int count);

cairo_surface_t *swaylogout_effects_run_timed(cairo_surface_t *surface, int scale,
		struct swaylogout_effect *effects, int count);

#endif
