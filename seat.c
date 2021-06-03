#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include "log.h"
#include "waylogout.h"
#include "seat.h"
#include "loop.h"

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
	struct waylogout_seat *seat = data;
	struct waylogout_state *state = seat->state;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		waylogout_log(LOG_ERROR, "Unknown keymap format %d, aborting", format);
		exit(1);
	}
	char *map_shm = mmap(NULL, size - 1, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map_shm == MAP_FAILED) {
		close(fd);
		waylogout_log(LOG_ERROR, "Unable to initialize keymap shm, aborting");
		exit(1);
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_buffer(
			state->xkb.context, map_shm, size - 1, XKB_KEYMAP_FORMAT_TEXT_V1,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_shm, size - 1);
	close(fd);
	assert(keymap);
	struct xkb_state *xkb_state = xkb_state_new(keymap);
	assert(xkb_state);
	xkb_keymap_unref(state->xkb.keymap);
	xkb_state_unref(state->xkb.state);
	state->xkb.keymap = keymap;
	state->xkb.state = xkb_state;
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	// Who cares
}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {
	// Who cares
}

static void keyboard_repeat(void *data) {
	struct waylogout_seat *seat = data;
	struct waylogout_state *state = seat->state;
	seat->repeat_timer = loop_add_timer(
		state->eventloop, seat->repeat_period_ms, keyboard_repeat, seat);
	waylogout_handle_key(state, seat->repeat_sym, seat->repeat_codepoint);
}

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t _key_state) {
	struct waylogout_seat *seat = data;
	struct waylogout_state *state = seat->state;
	enum wl_keyboard_key_state key_state = _key_state;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(state->xkb.state, key + 8);
	uint32_t keycode = key_state == WL_KEYBOARD_KEY_STATE_PRESSED ?
		key + 8 : 0;
	uint32_t codepoint = xkb_state_key_get_utf32(state->xkb.state, keycode);
	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		waylogout_handle_key(state, sym, codepoint);
	}

	if (seat->repeat_timer) {
		loop_remove_timer(seat->state->eventloop, seat->repeat_timer);
		seat->repeat_timer = NULL;
	}

	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED && seat->repeat_period_ms > 0) {
		seat->repeat_sym = sym;
		seat->repeat_codepoint = codepoint;
		seat->repeat_timer = loop_add_timer(
			seat->state->eventloop, seat->repeat_delay_ms, keyboard_repeat, seat);
	}
}

static void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	struct waylogout_seat *seat = data;
	struct waylogout_state *state = seat->state;
	xkb_state_update_mask(state->xkb.state,
		mods_depressed, mods_latched, mods_locked, 0, 0, group);
	// state->xkb.shift = xkb_state_mod_name_is_active(state->xkb.state,
	// 	XKB_MOD_NAME_SHIFT,
	// 	XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
		int32_t rate, int32_t delay) {
	struct waylogout_seat *seat = data;
	if (rate <= 0) {
		seat->repeat_period_ms = -1;
	} else {
		// Keys per second -> milliseconds between keys
		seat->repeat_period_ms = 1000 / rate;
	}
	seat->repeat_delay_ms = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct waylogout_state *state = data;
	wl_pointer_set_cursor(wl_pointer, serial, state->cursor_surface,
			state->cursor_image->hotspot_x, state->cursor_image->hotspot_y);
	waylogout_handle_mouse_enter(state, surface, surface_x, surface_y);
}

static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	waylogout_handle_mouse_leave((struct waylogout_state *)data, surface);
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	// surface_x, surface_y are relative coordinates when on a subsurface
	waylogout_handle_mouse_motion((struct waylogout_state *)data, surface_x, surface_y);
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	waylogout_handle_mouse_button((struct waylogout_state *)data,
			button, state);
}

static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	waylogout_handle_mouse_scroll((struct waylogout_state *)data,
			(axis ? 1 : -1) * value);
}

static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
	// Who cares
}

static void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis_source) {
	// Who cares
}

static void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis) {
	// Who cares
}

static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t discrete) {
	// Who cares
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = wl_pointer_enter,
	.leave = wl_pointer_leave,
	.motion = wl_pointer_motion,
	.button = wl_pointer_button,
	.axis = wl_pointer_axis,
	.frame = wl_pointer_frame,
	.axis_source = wl_pointer_axis_source,
	.axis_stop = wl_pointer_axis_stop,
	.axis_discrete = wl_pointer_axis_discrete,
};

static void wl_touch_down(void *data, struct wl_touch *touch, uint32_t serial,
		uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y) {
	waylogout_handle_touch_down((struct waylogout_state *)data, surface, id, x, y);
}

static void wl_touch_up(void *data, struct wl_touch *touch, uint32_t serial,
		uint32_t time, int32_t id) {
	waylogout_handle_touch_up((struct waylogout_state *)data, id);
}

static void wl_touch_motion(void *data, struct wl_touch *touch, uint32_t time,
		int32_t id, wl_fixed_t x, wl_fixed_t y) {
	waylogout_handle_touch_motion((struct waylogout_state *)data, id, x, y);
}

static void wl_touch_frame(void *data, struct wl_touch *touch) {
	// Who cares
}

static void wl_touch_cancel(void *data, struct wl_touch *touch) {
	// Who cares
}

static const struct wl_touch_listener touch_listener = {
	.down = wl_touch_down,
	.up = wl_touch_up,
	.motion = wl_touch_motion,
	.frame = wl_touch_frame,
	.cancel = wl_touch_cancel,
};

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct waylogout_seat *seat = data;
	if (seat->pointer) {
		wl_pointer_release(seat->pointer);
		seat->pointer = NULL;
	}
	if (seat->keyboard) {
		wl_keyboard_release(seat->keyboard);
		seat->keyboard = NULL;
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		seat->pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->pointer, &pointer_listener, seat->state);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		seat->keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(seat->keyboard, &keyboard_listener, seat);
	}
	if ((caps & WL_SEAT_CAPABILITY_TOUCH)) {
		seat->touch = wl_seat_get_touch(wl_seat);
		wl_touch_add_listener(seat->touch, &touch_listener, seat->state);
	}
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat,
		const char *name) {
	// Who cares
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};
