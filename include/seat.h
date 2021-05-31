#ifndef _SWAYLOGOUT_SEAT_H
#define _SWAYLOGOUT_SEAT_H
#include <xkbcommon/xkbcommon.h>
#include <stdint.h>
#include <stdbool.h>

struct loop;
struct loop_timer;

struct swaylogout_xkb {
	bool caps_lock;
	bool control;
	struct xkb_state *state;
	struct xkb_context *context;
	struct xkb_keymap *keymap;
};

struct swaylogout_seat {
	struct swaylogout_state *state;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_touch *touch;
	int32_t repeat_period_ms;
	int32_t repeat_delay_ms;
	uint32_t repeat_sym;
	uint32_t repeat_codepoint;
	struct loop_timer *repeat_timer;
};

extern const struct wl_seat_listener seat_listener;

#endif
