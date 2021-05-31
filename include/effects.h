#ifndef _WAYLOGOUT_EFFECTS_H
#define _WAYLOGOUT_EFFECTS_H

#include <stdbool.h>

#include "cairo.h"

struct waylogout_effect_screen_pos {
	float pos;
	bool is_percent;
};

struct waylogout_effect {
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
			struct waylogout_effect_screen_pos x;
			struct waylogout_effect_screen_pos y;
			struct waylogout_effect_screen_pos w;
			struct waylogout_effect_screen_pos h;
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

cairo_surface_t *waylogout_effects_run(cairo_surface_t *surface, int scale,
		struct waylogout_effect *effects, int count);

cairo_surface_t *waylogout_effects_run_timed(cairo_surface_t *surface, int scale,
		struct waylogout_effect *effects, int count);

#endif
