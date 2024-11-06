#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wordexp.h>
#include "background-image.h"
#include "cairo.h"
#include "log.h"
#include "loop.h"
#include "pool-buffer.h"
#include "seat.h"
#include "waylogout.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"

static const char *default_labels[WL_ACTION_END] = {
	'\0',
	"power off",
	"reboot",
	"sleep",
	"hibernate",
	"logout",
	"reload wm",
	"lock",
	"switch user",
	"cancel"
};

// populated in main()
char default_symbols[WL_ACTION_END][8];

static const xkb_keysym_t default_shortcuts[WL_ACTION_END] = {
	XKB_KEY_Escape,  // no action
	XKB_KEY_p,       // power off
	XKB_KEY_r,       // reboot
	XKB_KEY_s,       // sleep
	XKB_KEY_h,       // hibernate
	XKB_KEY_x,       // logout
	XKB_KEY_c,       // reload
	XKB_KEY_k,       // lock
	XKB_KEY_u,       // switch user
	XKB_KEY_c        // cancel
};


// returns a positive integer in milliseconds
static uint32_t parse_seconds(const char *seconds) {
	char *endptr;
	errno = 0;
	float val = strtof(seconds, &endptr);
	if (errno != 0) {
		waylogout_log(LOG_DEBUG, "Invalid number for seconds %s, defaulting to 0", seconds);
		return 0;
	}
	if (endptr == seconds) {
		waylogout_log(LOG_DEBUG, "No digits were found in %s, defaulting to 0", seconds);
		return 0;
	}
	if (val < 0) {
		waylogout_log(LOG_DEBUG, "Negative seconds not allowed for %s, defaulting to 0", seconds);
		return 0;
	}

	return (uint32_t)floor(val * 1000);
}

static uint32_t parse_color(const char *color) {
	if (color[0] == '#') {
		++color;
	}

	int len = strlen(color);
	if (len != 6 && len != 8) {
		waylogout_log(LOG_DEBUG, "Invalid color %s, defaulting to 0xFFFFFFFF",
				color);
		return 0xFFFFFFFF;
	}
	uint32_t res = (uint32_t)strtoul(color, NULL, 16);
	if (strlen(color) == 6) {
		res = (res << 8) | 0xFF;
	}
	return res;
}

static const char *parse_screen_pos(const char *str, struct waylogout_effect_screen_pos *pos) {
	char *eptr;
	float res = strtof(str, &eptr);
	if (eptr == str)
		return NULL;

	pos->pos = res;
	if (eptr[0] == '%') {
		pos->is_percent = true;
		return eptr + 1;
	} else {
		pos->is_percent = false;
		return eptr;
	}
}

static const char *parse_screen_pos_pair(const char *str, char delim,
		struct waylogout_effect_screen_pos *pos1,
		struct waylogout_effect_screen_pos *pos2) {
	struct waylogout_effect_screen_pos tpos1, tpos2;
	str = parse_screen_pos(str, &tpos1);
	if (str == NULL || str[0] != delim)
		return NULL;

	str = parse_screen_pos(str + 1, &tpos2);
	if (str == NULL)
		return NULL;

	pos1->pos = tpos1.pos;
	pos1->is_percent = tpos1.is_percent;
	pos2->pos = tpos2.pos;
	pos2->is_percent = tpos2.is_percent;
	return str;
}

static const char *parse_constant(const char *str1, const char *str2) {
	size_t len = strlen(str2);
	if (strncmp(str1, str2, len) == 0) {
		return str1 + len;
	} else {
		return NULL;
	}
}

static int parse_gravity_from_xy(float x, float y) {
	if (x >= 0 && y >= 0)
		return EFFECT_COMPOSE_GRAV_NW;
	else if (x >= 0 && y < 0)
		return EFFECT_COMPOSE_GRAV_SW;
	else if (x < 0 && y >= 0)
		return EFFECT_COMPOSE_GRAV_NE;
	else
		return EFFECT_COMPOSE_GRAV_SE;
}

static void parse_effect_compose(const char *str, struct waylogout_effect *effect) {
	effect->e.compose.x = effect->e.compose.y = (struct waylogout_effect_screen_pos) { 50, 1 }; // 50%
	effect->e.compose.w = effect->e.compose.h = (struct waylogout_effect_screen_pos) { -1, 0 }; // -1
	effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_CENTER;
	effect->e.compose.imgpath = NULL;

	// Parse position if they exist
	const char *s = parse_screen_pos_pair(str, ',', &effect->e.compose.x, &effect->e.compose.y);
	if (s == NULL) {
		s = str;
	} else {
		// If we're given an x/y position, determine gravity automatically
		// from whether x and y is positive or not
		effect->e.compose.gravity = parse_gravity_from_xy(
				effect->e.compose.x.pos, effect->e.compose.y.pos);
		s += 1;
		str = s;
	}

	// Parse dimensions if they exist
	s = parse_screen_pos_pair(str, 'x', &effect->e.compose.w, &effect->e.compose.h);
	if (s == NULL) {
		s = str;
	} else {
		s += 1;
		str = s;
	}

	// Parse gravity if it exists
	if ((s = parse_constant(str, "center;")) != NULL)
		effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_CENTER;
	else if ((s = parse_constant(str, "northwest;")) != NULL)
		effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_NW;
	else if ((s = parse_constant(str, "northeast;")) != NULL)
		effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_NE;
	else if ((s = parse_constant(str, "southwest;")) != NULL)
		effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_SW;
	else if ((s = parse_constant(str, "southeast;")) != NULL)
		effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_SE;
	else if ((s = parse_constant(str, "north;")) != NULL)
		effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_N;
	else if ((s = parse_constant(str, "south;")) != NULL)
		effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_S;
	else if ((s = parse_constant(str, "east;")) != NULL)
		effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_E;
	else if ((s = parse_constant(str, "west;")) != NULL)
		effect->e.compose.gravity = EFFECT_COMPOSE_GRAV_W;
	if (s == NULL) {
		s = str;
	} else {
		str = s;
	}

	// The rest is the file name
	effect->e.compose.imgpath = strdup(str);
}

int lenient_strcmp(char *a, char *b) {
	if (a == b) {
		return 0;
	} else if (!a) {
		return -1;
	} else if (!b) {
		return 1;
	} else {
		return strcmp(a, b);
	}
}

static void destroy_surface(struct waylogout_surface *surface) {
	waylogout_log(LOG_DEBUG, "Destroy surface for output %s", surface->output_name);

	wl_list_remove(&surface->link);
	if (surface->layer_surface != NULL) {
		zwlr_layer_surface_v1_destroy(surface->layer_surface);
	}
	if (surface->surface != NULL) {
		wl_surface_destroy(surface->surface);
	}
	destroy_buffer(&surface->buffers[0]);
	destroy_buffer(&surface->buffers[1]);
	struct waylogout_action *action;
	wl_list_for_each(action, &surface->state->actions, link) {
		destroy_buffer(&action->indicator_buffers[0]);
		destroy_buffer(&action->indicator_buffers[1]);
	}
	wl_output_destroy(surface->output);
	free(surface);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener;

static cairo_surface_t *select_image(struct waylogout_state *state,
		struct waylogout_surface *surface);

static bool surface_is_opaque(struct waylogout_surface *surface) {
	if (!fade_is_complete(&surface->fade)) {
		return false;
	}
	if (surface->image) {
		return cairo_surface_get_content(surface->image) == CAIRO_CONTENT_COLOR;
	}
	return (surface->state->args.colors.background & 0xff) == 0xff;
}

static void create_surface(struct waylogout_surface *surface) {
	struct waylogout_state *state = surface->state;

	if (state->args.allow_fade && state->args.fade_in) {
		surface->fade.target_time = state->args.fade_in;
	}

	surface->image = select_image(state, surface);

	surface->surface = wl_compositor_create_surface(state->compositor);
	assert(surface->surface);

	wl_array_init(&surface->children);
	struct waylogout_action *action;
	wl_list_for_each(action, &state->actions, link) {
		struct waylogout_action_surface *new_child =
			(struct waylogout_action_surface *) wl_array_add(&surface->children, sizeof(struct waylogout_action_surface));
		new_child->action = action;
		new_child->parent_surface = surface;
		new_child->surface = wl_compositor_create_surface(state->compositor);
		assert(new_child->surface);
		new_child->subsurface = wl_subcompositor_get_subsurface(
				state->subcompositor, new_child->surface,
				surface->surface);
		assert(new_child->subsurface);
		wl_subsurface_set_sync(new_child->subsurface);
	}

	surface->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			state->layer_shell, surface->surface, surface->output,
			ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "logout_dialog");

	zwlr_layer_surface_v1_set_size(surface->layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_anchor(surface->layer_surface,
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_exclusive_zone(surface->layer_surface, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(
			surface->layer_surface, true);
	zwlr_layer_surface_v1_add_listener(surface->layer_surface,
			&layer_surface_listener, surface);
	surface->events_pending += 1;

	wl_surface_commit(surface->surface);
}

static void create_cursor_surface(struct waylogout_state *state) {
	struct wl_cursor_theme *cursor_theme = wl_cursor_theme_load(NULL, 24, state->shm);
	struct wl_cursor *cursor = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
	state->cursor_image = cursor->images[0];
	struct wl_buffer *cursor_buffer = wl_cursor_image_get_buffer(state->cursor_image);

	state->cursor_surface = wl_compositor_create_surface(state->compositor);
	wl_surface_attach(state->cursor_surface, cursor_buffer, 0, 0);
	wl_surface_commit(state->cursor_surface);
}

static void initially_render_surface(struct waylogout_surface *surface) {
	waylogout_log(LOG_DEBUG, "Surface for output %s ready", surface->output_name);
	if (surface_is_opaque(surface) &&
			surface->state->args.mode != BACKGROUND_MODE_CENTER &&
			surface->state->args.mode != BACKGROUND_MODE_FIT) {
		struct wl_region *region =
			wl_compositor_create_region(surface->state->compositor);
		wl_region_add(region, 0, 0, INT32_MAX, INT32_MAX);
		wl_surface_set_opaque_region(surface->surface, region);
		wl_region_destroy(region);
	}

	render_frame_background(surface, true);
	render_frames(surface);
}

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	surface->width = width;
	surface->height = height;
	struct waylogout_action_surface *action_surface;
	wl_array_for_each(action_surface, &surface->children) {
		action_surface->indicator_width = 0;
		action_surface->indicator_height = 0;
	}
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	if (!surface->configured && --surface->events_pending == 0) {
		initially_render_surface(surface);
	}
	surface->configured = true;
}

static void layer_surface_closed(void *data,
		struct zwlr_layer_surface_v1 *layer_surface) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	destroy_surface(surface);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static const struct wl_callback_listener surface_frame_listener;

static void surface_frame_handle_done(void *data, struct wl_callback *callback,
		uint32_t time) {
	struct waylogout_surface *surface = data;

	wl_callback_destroy(callback);
	surface->frame_pending = false;

	if (surface->dirty) {
		// Schedule a frame in case the surface is damaged again
		struct wl_callback *callback = wl_surface_frame(surface->surface);
		wl_callback_add_listener(callback, &surface_frame_listener, surface);
		surface->frame_pending = true;
		surface->dirty = false;

		if (!fade_is_complete(&surface->fade)) {
			render_background_fade(surface, time);
			surface->dirty = true;
		}

		render_frames(surface);
	}
}

static const struct wl_callback_listener surface_frame_listener = {
	.done = surface_frame_handle_done,
};

void damage_surface(struct waylogout_surface *surface) {
	if (surface->width == 0 || surface->height == 0) {
		// Not yet configured
		return;
	}

	surface->dirty = true;
	if (surface->frame_pending) {
		return;
	}

	struct wl_callback *callback = wl_surface_frame(surface->surface);
	wl_callback_add_listener(callback, &surface_frame_listener, surface);
	surface->frame_pending = true;
	wl_surface_commit(surface->surface);
}

void damage_state(struct waylogout_state *state) {
	struct waylogout_surface *surface;
	wl_list_for_each(surface, &state->surfaces, link) {
		damage_surface(surface);
	}
}

static void handle_wl_output_geometry(void *data, struct wl_output *wl_output,
		int32_t x, int32_t y, int32_t width_mm, int32_t height_mm,
		int32_t subpixel, const char *make, const char *model,
		int32_t transform) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	surface->subpixel = subpixel;
	surface->transform = transform;
	if (surface->state->run_display) {
		damage_surface(surface);
	}
}

static void handle_wl_output_mode(void *data, struct wl_output *output,
		uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
	// Who cares
}

static void handle_wl_output_scale(void *data, struct wl_output *output,
		int32_t factor) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	surface->scale = factor;
	if (surface->state->run_display) {
		damage_surface(surface);
	}
}

static struct wl_buffer *create_shm_buffer(struct wl_shm *shm, enum wl_shm_format fmt,
		int width, int height, int stride, void **data_out) {
	int size = stride * height;

	const char shm_name[] = "/waylogout-shm";
	int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "shm_open failed\n");
		return NULL;
	}
	shm_unlink(shm_name);

	int ret;
	while ((ret = ftruncate(fd, size)) == EINTR) {
		// No-op
	}
	if (ret < 0) {
		close(fd);
		fprintf(stderr, "ftruncate failed\n");
		return NULL;
	}

	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	close(fd);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
		stride, fmt);
	wl_shm_pool_destroy(pool);

	*data_out = data;
	return buffer;
}

static cairo_surface_t *apply_effects(cairo_surface_t *image, struct waylogout_state *state, int scale) {
	if (state->args.effects_count == 0) {
		return image;
	}

	if (state->args.time_effects) {
		return waylogout_effects_run_timed(
				image, scale,
				state->args.effects, state->args.effects_count);
	} else {
		return waylogout_effects_run(
				image, scale,
				state->args.effects, state->args.effects_count);
	}
}

static void handle_screencopy_frame_buffer(void *data,
		struct zwlr_screencopy_frame_v1 *frame, uint32_t format, uint32_t width,
		uint32_t height, uint32_t stride) {
	waylogout_trace();
	struct waylogout_surface *surface = data;

	struct waylogout_image *image = calloc(1, sizeof(struct waylogout_image));
	image->path = NULL;
	image->output_name = surface->output_name;

	void *bufdata;
	struct wl_buffer *buf = create_shm_buffer(surface->state->shm, format, width, height, stride, &bufdata);
	if (buf == NULL) {
		free(image);
		return;
	}

	surface->screencopy.format = format;
	surface->screencopy.width = width;
	surface->screencopy.height = height;
	surface->screencopy.stride = stride;

	surface->screencopy.image = image;
	surface->screencopy.data = bufdata;

	zwlr_screencopy_frame_v1_copy(frame, buf);
}

static void handle_screencopy_frame_flags(void *data,
		struct zwlr_screencopy_frame_v1 *frame, uint32_t flags) {
	waylogout_trace();
	struct waylogout_surface *surface = data;

	// The transform affecting a screenshot consists of three parts:
	// Whether it's flipped vertically, whether it's flipped horizontally,
	// and the four rotation options (0, 90, 180, 270).
	// Any of the combinations of vertical flips, horizontal flips and rotation,
	// can be expressed in terms of only horizontal flips and rotation
	// (which is what the enum wl_output_transform encodes).
	// Therefore, instead of inverting the Y axis or keeping around the
	// "was it vertically flipped?" bit, we just map our state space onto the
	// state space encoded by wl_output_transform and let load_background_from_buffer
	// handle the rest.
	if (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) {
		switch (surface->transform) {
		case WL_OUTPUT_TRANSFORM_NORMAL:
			surface->screencopy.transform = WL_OUTPUT_TRANSFORM_FLIPPED_180;
			break;
		case WL_OUTPUT_TRANSFORM_90:
			surface->screencopy.transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
			break;
		case WL_OUTPUT_TRANSFORM_180:
			surface->screencopy.transform = WL_OUTPUT_TRANSFORM_FLIPPED;
			break;
		case WL_OUTPUT_TRANSFORM_270:
			surface->screencopy.transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED:
			surface->screencopy.transform = WL_OUTPUT_TRANSFORM_180;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_90:
			surface->screencopy.transform = WL_OUTPUT_TRANSFORM_90;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_180:
			surface->screencopy.transform = WL_OUTPUT_TRANSFORM_NORMAL;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_270:
			surface->screencopy.transform = WL_OUTPUT_TRANSFORM_270;
			break;
		}
	} else {
		surface->screencopy.transform = surface->transform;
	}
}

static void handle_screencopy_frame_ready(void *data,
		struct zwlr_screencopy_frame_v1 *frame, uint32_t tv_sec_hi,
		uint32_t tv_sec_lo, uint32_t tv_nsec) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	struct waylogout_state *state = surface->state;

	cairo_surface_t *image = load_background_from_buffer(
			surface->screencopy.data,
			surface->screencopy.format,
			surface->screencopy.width,
			surface->screencopy.height,
			surface->screencopy.stride,
			surface->screencopy.transform);
	if (image == NULL) {
		waylogout_log(LOG_ERROR, "Failed to create image from screenshot");
		state->args.screenshots = false;
		state->args.fade_in = 0; // Fade in is not possible without screenshot
	} else  {
		surface->screencopy.original_image = cairo_surface_duplicate(image);
		surface->screencopy.image->cairo_surface = image;
		if (state->args.screenshots) {
			waylogout_log(LOG_DEBUG, "Loaded screenshot for output %s", surface->output_name);
			wl_list_insert(&state->images, &surface->screencopy.image->link);
		}
	}

	--surface->events_pending;
}

static void handle_screencopy_frame_failed(void *data,
		struct zwlr_screencopy_frame_v1 *frame) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	waylogout_log(LOG_ERROR, "Screencopy failed");
	surface->state->args.screenshots = false;
	surface->state->args.fade_in = 0; // Fade in is not possible without screenshot

	--surface->events_pending;
}

static const struct zwlr_screencopy_frame_v1_listener screencopy_frame_listener = {
	.buffer = handle_screencopy_frame_buffer,
	.flags = handle_screencopy_frame_flags,
	.ready = handle_screencopy_frame_ready,
	.failed = handle_screencopy_frame_failed,
};

static void handle_wl_output_name(void *data, struct wl_output *output,
		const char *name) {
	waylogout_trace();
	waylogout_log(LOG_DEBUG, "output name is %s", name);
	struct waylogout_surface *surface = data;
	surface->output_name = strdup(name);
}

static void handle_wl_output_description(void *data, struct wl_output *output,
		const char *description) {
	// Who cares
}

static void handle_wl_output_done(void *data, struct wl_output *output) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	struct waylogout_state *state = surface->state;

	static bool has_printed_screencopy_error = false;
	if (state->screencopy_manager) {
		surface->screencopy_frame = zwlr_screencopy_manager_v1_capture_output(
				state->screencopy_manager, false, surface->output);
		zwlr_screencopy_frame_v1_add_listener(surface->screencopy_frame,
				&screencopy_frame_listener, surface);
		surface->events_pending += 1;
	} else if (!has_printed_screencopy_error) {
		waylogout_log(LOG_INFO, "Compositor does not support screencopy manager, "
				"screenshots / fade-in will not work");
		state->args.screenshots = false;
		state->args.fade_in = 0; // Fade in is not possible without screenshot
		has_printed_screencopy_error = true;
	}

	--surface->events_pending;
}

struct wl_output_listener _wl_output_listener = {
	.geometry = handle_wl_output_geometry,
	.mode = handle_wl_output_mode,
	.done = handle_wl_output_done,
	.scale = handle_wl_output_scale,
	.name = handle_wl_output_name,
	.description = handle_wl_output_description,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {

	struct waylogout_state *state = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
		state->subcompositor = wl_registry_bind(registry, name,
				&wl_subcompositor_interface, 1);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name,
				&wl_shm_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct wl_seat *seat = wl_registry_bind(
				registry, name, &wl_seat_interface, 4);
		struct waylogout_seat *waylogout_seat =
			calloc(1, sizeof(struct waylogout_seat));
		waylogout_seat->state = state;
		wl_seat_add_listener(seat, &seat_listener, waylogout_seat);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = wl_registry_bind(
				registry, name, &zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct waylogout_surface *surface =
			calloc(1, sizeof(struct waylogout_surface));
		surface->state = state;
		surface->output = wl_registry_bind(registry, name,
				&wl_output_interface, 4);
		surface->output_global_name = name;
		wl_output_add_listener(surface->output, &_wl_output_listener, surface);
		wl_list_insert(&state->surfaces, &surface->link);

		if (state->run_display) {
			create_surface(surface);
			wl_display_roundtrip(state->display);
		}
	} else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
		state->screencopy_manager = wl_registry_bind(registry, name,
				&zwlr_screencopy_manager_v1_interface, 1);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	struct waylogout_state *state = data;
	struct waylogout_surface *surface;
	wl_list_for_each(surface, &state->surfaces, link) {
		if (surface->output_global_name == name) {
			destroy_surface(surface);
			break;
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static cairo_surface_t *select_image(struct waylogout_state *state,
		struct waylogout_surface *surface) {
	struct waylogout_image *image;
	cairo_surface_t *default_image = NULL;
	wl_list_for_each(image, &state->images, link) {
		if (lenient_strcmp(image->output_name, surface->output_name) == 0) {
			return image->cairo_surface;
		} else if (!image->output_name) {
			default_image = image->cairo_surface;
		}
	}
	return default_image;
}

static char *join_args(char **argv, int argc) {
	assert(argc > 0);
	int len = 0, i;
	for (i = 0; i < argc; ++i) {
		len += strlen(argv[i]) + 1;
	}
	char *res = malloc(len);
	len = 0;
	for (i = 0; i < argc; ++i) {
		strcpy(res + len, argv[i]);
		len += strlen(argv[i]);
		res[len++] = ' ';
	}
	res[len - 1] = '\0';
	return res;
}

static char *strdup_noquotes(char *input) {
	if (input == NULL)
		return NULL;
	char *s = strdup(input);
	char *s_copy = s;
	size_t lastpos = strlen(s) - 1;
	if (lastpos > 0) {
		if (
				(s[0] == '"'  && s[lastpos] == '"' )
			||  (s[0] == '\'' && s[lastpos] == '\'')
		) {
			s[lastpos] = '\0';
			++s;
		}
	}
	char *retptr = strdup(s);
	free(s_copy);
	return retptr;
}

static void load_image(char *arg, struct waylogout_state *state) {
	// [[<output>]:]<path>
	struct waylogout_image *image = calloc(1, sizeof(struct waylogout_image));
	char *separator = strchr(arg, ':');
	if (separator) {
		*separator = '\0';
		image->output_name = separator == arg ? NULL : strdup(arg);
		image->path = strdup_noquotes(separator + 1);
	} else {
		image->output_name = NULL;
		image->path = strdup_noquotes(arg);
	}

	struct waylogout_image *iter_image, *temp;
	wl_list_for_each_safe(iter_image, temp, &state->images, link) {
		if (lenient_strcmp(iter_image->output_name, image->output_name) == 0) {
			if (image->output_name) {
				waylogout_log(LOG_DEBUG,
						"Replacing image defined for output %s with %s",
						image->output_name, image->path);
			} else {
				waylogout_log(LOG_DEBUG, "Replacing default image with %s",
						image->path);
			}
			wl_list_remove(&iter_image->link);
			free(iter_image->cairo_surface);
			free(iter_image->output_name);
			free(iter_image->path);
			free(iter_image);
			break;
		}
	}

	// The shell will not expand ~ to the value of $HOME when an output name is
	// given. Also, any image paths given in the config file need to have shell
	// expansions performed
	wordexp_t p;
	while (strstr(image->path, "  ")) {
		image->path = realloc(image->path, strlen(image->path) + 2);
		char *ptr = strstr(image->path, "  ") + 1;
		memmove(ptr + 1, ptr, strlen(ptr) + 1);
		*ptr = '\\';
	}
	if (wordexp(image->path, &p, 0) == 0) {
		free(image->path);
		image->path = join_args(p.we_wordv, p.we_wordc);
		wordfree(&p);
	}

	// Load the actual image
	image->cairo_surface = load_background_image(image->path);
	if (!image->cairo_surface) {
		free(image);
		return;
	}

	wl_list_insert(&state->images, &image->link);
	waylogout_log(LOG_DEBUG, "Loaded image %s for output %s", image->path,
			image->output_name ? image->output_name : "*");
}

static void set_default_colors(struct waylogout_colors *colors) {
	colors->background = 0xFFFFFFFF;
	colors->inside = (struct waylogout_colorset){
		.normal = 0x000000C0,
		.selected = 0xFA0000C0,
	};
	colors->line = (struct waylogout_colorset){
		.normal = 0x000000FF,
		.selected = 0x000000FF,
	};
	colors->ring = (struct waylogout_colorset){
		.normal = 0x337D00FF,
		.selected = 0x7D3300FF,
	};
	colors->text = (struct waylogout_colorset){
		.normal = 0xE5A445FF,
		.selected = 0x000000FF,
	};
}

enum line_mode {
	LM_LINE,
	LM_INSIDE,
	LM_RING,
};

static void add_action(struct waylogout_state *state,
		enum waylogout_action_type type, char *label, char *symbol,
		char *command, xkb_keysym_t shortcut) {

	struct waylogout_action *action;
	wl_list_for_each(action, &state->actions, link)
		if (type == action->type)
			return;

	struct waylogout_action *new_action = malloc(sizeof(struct waylogout_action));

	new_action->type = type;
	new_action->label = strdup_noquotes(label); // safe to pass NULL to strdup_noquotes
	if (symbol)
		strncpy(new_action->symbol, symbol, 4);
	else
		new_action->symbol[0] = '\0';
	new_action->command = strdup_noquotes(command); // safe to pass NULL to strdup_noquotes
	new_action->shortcut = shortcut;
	new_action->rendered_depressed = false;

	for (size_t i = 0; i < 2; ++i)
		new_action->indicator_buffers[i] = (struct pool_buffer){
			.buffer = NULL,
			.surface = NULL,
			.cairo = NULL,
			.width = 0,
			.height = 0,
			.data = NULL,
			.size = 0,
			.busy = false
		};

	// insert new action at end of list
	wl_list_insert(state->actions.prev, &new_action->link);

}

static void add_action_label(struct waylogout_state *state,
		enum waylogout_action_type type, char *label) {
	struct waylogout_action *action;
	wl_list_for_each(action, &state->actions, link) {
		if (type == action->type) {
			action->label = strdup_noquotes(label);
			return;
		}
	}
	add_action(state, type, label, NULL, NULL, XKB_KEY_VoidSymbol);
}

static void add_action_symbol(struct waylogout_state *state,
		enum waylogout_action_type type, char *symbol) {
	struct waylogout_action *action;
	wl_list_for_each(action, &state->actions, link) {
		if (type == action->type) {
			strncpy(action->symbol, symbol, 4);
			return;
		}
	}
	add_action(state, type, NULL, symbol, NULL, XKB_KEY_VoidSymbol);
}

static void add_action_command(struct waylogout_state *state,
		enum waylogout_action_type type, char *command) {
	struct waylogout_action *action;
	wl_list_for_each(action, &state->actions, link) {
		if (type == action->type) {
			action->command = strdup_noquotes(command);
			return;
		}
	}
	add_action(state, type, NULL, NULL, command, XKB_KEY_VoidSymbol);
}

static void set_default_action(struct waylogout_state *state) {
	enum waylogout_action_type default_type;
	if (lenient_strcmp(state->args.default_action, "poweroff") == 0)
		default_type = WL_ACTION_POWEROFF;
	else if (lenient_strcmp(state->args.default_action, "reboot") == 0)
		default_type = WL_ACTION_REBOOT;
	else if (lenient_strcmp(state->args.default_action, "suspend") == 0)
		default_type = WL_ACTION_SUSPEND;
	else if (lenient_strcmp(state->args.default_action, "hibernate") == 0)
		default_type = WL_ACTION_HIBERNATE;
	else if (lenient_strcmp(state->args.default_action, "logout") == 0)
		default_type = WL_ACTION_LOGOUT;
	else if (lenient_strcmp(state->args.default_action, "reload") == 0)
		default_type = WL_ACTION_RELOAD;
	else if (lenient_strcmp(state->args.default_action, "lock") == 0)
		default_type = WL_ACTION_LOCK;
	else if (lenient_strcmp(state->args.default_action, "switch-user") == 0)
		default_type = WL_ACTION_SWITCH;
	else
		default_type = WL_ACTION_NO_ACTION;

	if (default_type == WL_ACTION_NO_ACTION) {
		waylogout_log(LOG_DEBUG, "No default action configured");
		return;
	}

	bool found_default = false;
	struct waylogout_action *action;
	wl_list_for_each(action, &state->actions, link) {
		if (default_type == action->type) {
			state->selected_action = action;
			found_default = true;
		}
	}
	if (found_default)
		waylogout_log(LOG_INFO, "Set default action to %s", state->args.default_action);
	else
		waylogout_log(LOG_ERROR, "Requested default action is %s, but that action has not been configured", state->args.default_action);
}

void run_action(struct waylogout_action *action) {
	if (!action)
		return;
	waylogout_log(LOG_DEBUG, "Running %s action", action->label);
	waylogout_log(LOG_DEBUG, "%s", action->command);
	char *const cmd[] = { "sh", "-c", action->command, NULL, };
	execvp(cmd[0], cmd);
}

static void setup_rows(struct waylogout_state *state) {
	if (state->args.rows > state->n_actions) {
		waylogout_log(LOG_INFO, "Requested number of rows is greater than number of configured actions.");
		state->args.rows = state->n_actions;
	}
	int per_row = state->n_actions / state->args.rows;
	state->longest_row = per_row;
	uint8_t leftover = state->n_actions % state->args.rows;
	if (leftover > 0)
		++state->longest_row;
	uint8_t shorter = state->args.rows - leftover;
	uint8_t row_tip = (shorter == 1) ? 0 : (shorter / 2 - 1);
	uint8_t row_index;
	for (row_index = 0; row_index < state->args.rows; ++row_index) {
		state->rows[row_index] = per_row;
		if ((row_index > row_tip) && (leftover > 0)) {
			++state->rows[row_index];
			--leftover;
		}
	}
	row_index = 0;
	int n_actions_this_row = 0;
	struct waylogout_action *action;
	wl_list_for_each(action, &state->actions, link) {
		action->row = row_index;
		++n_actions_this_row;
		if (n_actions_this_row == state->rows[row_index]) {
			++row_index;
			n_actions_this_row = 0;
		}
	}
}

static int parse_options(int argc, char **argv, struct waylogout_state *state,
		enum line_mode *line_mode, char **config_path) {
	enum long_option_codes {
		LO_FONT = 256,
		LO_FA_FONT,
		LO_SYMBOL_FONT_SIZE,
		LO_LABEL_FONT_SIZE,
		LO_DEFAULT_ACTION,
		LO_IND_RADIUS,
		LO_IND_X_POSITION,
		LO_IND_Y_POSITION,
		LO_IND_SEP,
		LO_IND_THICKNESS,
		LO_INSIDE_COLOR,
		LO_INSIDE_HL_COLOR,
		LO_LINE_COLOR,
		LO_LINE_WRONG_COLOR,
		LO_LINE_HL_COLOR,
		LO_RING_COLOR,
		LO_RING_HL_COLOR,
		LO_TEXT_COLOR,
		LO_TEXT_HL_COLOR,
		LO_EFFECT_BLUR,
		LO_EFFECT_PIXELATE,
		LO_EFFECT_SCALE,
		LO_EFFECT_GREYSCALE,
		LO_EFFECT_VIGNETTE,
		LO_EFFECT_COMPOSE,
		LO_EFFECT_CUSTOM,
		LO_TIME_EFFECTS,
		LO_FADE_IN,
		LO_LABELS,
		LO_SELECTION_LABEL,
		LO_HIDE_CANCEL,
		LO_REVERSE_ARROWS,
		LO_COMMAND_POWEROFF,
		LO_COMMAND_REBOOT,
		LO_COMMAND_SUSPEND,
		LO_COMMAND_HIBERNATE,
		LO_COMMAND_LOGOUT,
		LO_COMMAND_RELOAD,
		LO_COMMAND_LOCK,
		LO_COMMAND_SWITCH,
		LO_LABEL_POWEROFF,
		LO_LABEL_REBOOT,
		LO_LABEL_SUSPEND,
		LO_LABEL_HIBERNATE,
		LO_LABEL_LOGOUT,
		LO_LABEL_RELOAD,
		LO_LABEL_LOCK,
		LO_LABEL_SWITCH,
		LO_LABEL_CANCEL,
		LO_SYMBOL_POWEROFF,
		LO_SYMBOL_REBOOT,
		LO_SYMBOL_SUSPEND,
		LO_SYMBOL_HIBERNATE,
		LO_SYMBOL_LOGOUT,
		LO_SYMBOL_RELOAD,
		LO_SYMBOL_LOCK,
		LO_SYMBOL_SWITCH,
		LO_SYMBOL_CANCEL,
		LO_SCROLL_SENSITIVITY,
		LO_INSTANT_RUN,
	};

	static struct option long_options[] = {
		{"config", required_argument, NULL, 'C'},
		{"color", required_argument, NULL, 'c'},
		{"debug", no_argument, NULL, 'd'},
		{"trace", no_argument, NULL, 't'},
		{"help", no_argument, NULL, 'h'},
		{"image", required_argument, NULL, 'i'},
		{"labels", no_argument, NULL, 'l'},
		{"line-uses-inside", no_argument, NULL, 'n'},
		{"line-uses-ring", no_argument, NULL, 'r'},
		{"rows", required_argument, NULL, 'R'},
		{"screenshots", no_argument, NULL, 'S'},
		{"scaling", required_argument, NULL, 's'},
		{"tiling", no_argument, NULL, 'T'},
		{"version", no_argument, NULL, 'v'},
		{"selection-label", no_argument, NULL, LO_SELECTION_LABEL},
		{"font", required_argument, NULL, LO_FONT},
		{"fa-font", required_argument, NULL, LO_FA_FONT},
		{"symbol-font-size", required_argument, NULL, LO_SYMBOL_FONT_SIZE},
		{"label-font-size", required_argument, NULL, LO_LABEL_FONT_SIZE},
		{"indicator-radius", required_argument, NULL, LO_IND_RADIUS},
		{"indicator-thickness", required_argument, NULL, LO_IND_THICKNESS},
		{"indicator-x-position", required_argument, NULL, LO_IND_X_POSITION},
		{"indicator-y-position", required_argument, NULL, LO_IND_Y_POSITION},
		{"indicator-separation", required_argument, NULL, LO_IND_SEP},
		{"inside-color", required_argument, NULL, LO_INSIDE_COLOR},
		{"inside-selection-color", required_argument, NULL, LO_INSIDE_HL_COLOR},
		{"line-color", required_argument, NULL, LO_LINE_COLOR},
		{"line-selection-color", required_argument, NULL, LO_LINE_HL_COLOR},
		{"ring-color", required_argument, NULL, LO_RING_COLOR},
		{"ring-selection-color", required_argument, NULL, LO_RING_HL_COLOR},
		{"text-color", required_argument, NULL, LO_TEXT_COLOR},
		{"text-selection-color", required_argument, NULL, LO_TEXT_HL_COLOR},
		{"effect-blur", required_argument, NULL, LO_EFFECT_BLUR},
		{"effect-pixelate", required_argument, NULL, LO_EFFECT_PIXELATE},
		{"effect-scale", required_argument, NULL, LO_EFFECT_SCALE},
		{"effect-greyscale", no_argument, NULL, LO_EFFECT_GREYSCALE},
		{"effect-vignette", required_argument, NULL, LO_EFFECT_VIGNETTE},
		{"effect-compose", required_argument, NULL, LO_EFFECT_COMPOSE},
		{"effect-custom", required_argument, NULL, LO_EFFECT_CUSTOM},
		{"time-effects", no_argument, NULL, LO_TIME_EFFECTS},
		{"fade-in", required_argument, NULL, LO_FADE_IN},
		{"poweroff-command", required_argument, NULL, LO_COMMAND_POWEROFF},
		{"reboot-command", required_argument, NULL, LO_COMMAND_REBOOT},
		{"suspend-command", required_argument, NULL, LO_COMMAND_SUSPEND},
		{"hibernate-command", required_argument, NULL, LO_COMMAND_HIBERNATE},
		{"logout-command", required_argument, NULL, LO_COMMAND_LOGOUT},
		{"reload-command", required_argument, NULL, LO_COMMAND_RELOAD},
		{"lock-command", required_argument, NULL, LO_COMMAND_LOCK},
		{"switch-user-command", required_argument, NULL, LO_COMMAND_SWITCH},
		{"poweroff-label", required_argument, NULL, LO_LABEL_POWEROFF},
		{"reboot-label", required_argument, NULL, LO_LABEL_REBOOT},
		{"suspend-label", required_argument, NULL, LO_LABEL_SUSPEND},
		{"hibernate-label", required_argument, NULL, LO_LABEL_HIBERNATE},
		{"logout-label", required_argument, NULL, LO_LABEL_LOGOUT},
		{"reload-label", required_argument, NULL, LO_LABEL_RELOAD},
		{"lock-label", required_argument, NULL, LO_LABEL_LOCK},
		{"switch-user-label", required_argument, NULL, LO_LABEL_SWITCH},
		{"cancel-label", required_argument, NULL, LO_LABEL_CANCEL},
		{"poweroff-symbol", required_argument, NULL, LO_SYMBOL_POWEROFF},
		{"reboot-symbol", required_argument, NULL, LO_SYMBOL_REBOOT},
		{"suspend-symbol", required_argument, NULL, LO_SYMBOL_SUSPEND},
		{"hibernate-symbol", required_argument, NULL, LO_SYMBOL_HIBERNATE},
		{"logout-symbol", required_argument, NULL, LO_SYMBOL_LOGOUT},
		{"reload-symbol", required_argument, NULL, LO_SYMBOL_RELOAD},
		{"lock-symbol", required_argument, NULL, LO_SYMBOL_LOCK},
		{"switch-user-symbol", required_argument, NULL, LO_SYMBOL_SWITCH},
		{"cancel-symbol", required_argument, NULL, LO_SYMBOL_CANCEL},
		{"default-action", required_argument, NULL, LO_DEFAULT_ACTION},
		{"hide-cancel", no_argument, NULL, LO_HIDE_CANCEL},
		{"reverse-arrows", no_argument, NULL, LO_REVERSE_ARROWS},
		{"scroll-sensitivity", required_argument, NULL, LO_SCROLL_SENSITIVITY},
		{"instant-run", no_argument, NULL, LO_INSTANT_RUN},
		{0, 0, 0, 0}
	};

	const char usage[] =
		"Usage: waylogout [options...]\n"
		"\n"
		"  -C, --config <config_file>       "
			"Path to the config file.\n"
		"  -c, --color <color>              "
			"Turn the screen into the given color instead of white.\n"
		"  -d, --debug                      "
			"Enable debugging output.\n"
		"  -t, --trace                      "
			"Enable tracing output.\n"
		"  -h, --help                       "
			"Show help message and quit.\n"
		"  -i, --image [[<output>]:]<path>  "
			"Display the given image, optionally only on the given output.\n"
		"  -l, --labels                     "
			"Show action labels.\n"
		"  -R, --rows <number>              "
			"Number of rows to use to display action indicators. Default: 1.\n"
		"  -S, --screenshots                "
			"Use screenshots as the background images.\n"
		"  -s, --scaling <mode>             "
			"Image scaling mode: stretch, fill, fit, center, tile, solid_color.\n"
		"  -T, --tiling                     "
			"Same as --scaling=tile.\n"
		"  -v, --version                    "
			"Show the version number and quit.\n"
		"  --fade-in <seconds>              "
			"Make the logout screen fade in instead of just popping in.\n"
		"  --selection-label                 "
			"Always show label on selected action.\n"
		"  --font <font>                    "
			"Sets the font of the action label text.\n"
		"  --label-font-size <size>         "
			"Sets a fixed font size for the action label text.\n"
		"  --fa-font <font>                 "
			"Sets the name of the Font Awesome font. Default is 'Font Awesome 6 Free'.\n"
		"  --symbol-font-size <size>        "
			"Sets a fixed font size for the action symbol.\n"
		"  --indicator-radius <radius>      "
			"Sets the action indicator radius.\n"
		"  --indicator-thickness <thick>    "
			"Sets the action indicator thickness.\n"
		"  --indicator-x-position <x>       "
			"Sets the horizontal centre position of the action indicator array.\n"
		"  --indicator-y-position <y>       "
			"Sets the vertical centre position of the action indicator array.\n"
		"  --indicator-separation <sep>     "
			"Sets a fixed amount of space separating action indicators.\n"
		"  --inside-color <color>           "
			"Sets the color of the inside of the action indicators.\n"
		"  --inside-selection-color <color>  "
			"Sets the color of the inside of the selected action indicator.\n"
		"  --line-color <color>             "
			"Sets the color of the line between the inside and ring.\n"
		"  --line-selection-color <color>    "
			"Sets the color of the line between the inside and ring in "
			"the selected action indicator.\n"
		"  -n, --line-uses-inside           "
			"Use the inside color for the line between the inside and ring.\n"
		"  -r, --line-uses-ring             "
			"Use the ring color for the line between the inside and ring.\n"
		"  --ring-color <color>             "
			"Sets the color of the ring of the action indicators.\n"
		"  --ring-selection-color <color>    "
			"Sets the color of the ring of the selected action indicator.\n"
		"  --text-color <color>             "
			"Sets the color of the text.\n"
		"  --text-selection-color <color>    "
			"Sets the color of the text for the selected action indicator.\n"
		"  --effect-blur <radius>x<times>   "
			"Blur images.\n"
		"  --effect-pixelate <factor>       "
			"Pixelate images.\n"
		"  --effect-scale <scale>           "
			"Scale images.\n"
		"  --effect-greyscale               "
			"Make images greyscale.\n"
		"  --effect-vignette <base>:<factor>"
			"Apply a vignette effect to images. Base and factor should be numbers between 0 and 1.\n"
		"  --effect-custom <path>           "
			"Apply a custom effect from a shared object or C source file.\n"
		"  --time-effects                   "
			"Measure the time it takes to run each effect.\n"
		"  --poweroff-command <command>     "
		    "Command to run when \"poweroff\" action is activated.\n"
		"  --poweroff-label <label>         "
		    "Custom text label to display in the indicator for the \"poweroff\" action. Default is \"power off\".\n"
		"  --poweroff-symbol <symbol>       "
		    "Custom UTF-8 symbol character to display in the indicator for the \"poweroff\" action. Default is .\n"
		"  --reboot-command <command>       "
		    "Command to run when \"reboot\" action is activated.\n"
		"  --reboot-label <label>           "
		    "Custom text label to display in the indicator for the \"reboot\" action. Default is \"reboot\".\n"
		"  --reboot-symbol <symbol>         "
		    "Custom UTF-8 symbol character to display in the indicator for the \"reboot\" action. Default is .\n"
		"  --suspend-command <command>      "
		    "Command to run when \"suspend\" action is activated.\n"
		"  --suspend-label <label>          "
		    "Custom text label to display in the indicator for the \"suspend\" action. Default is \"suspend\".\n"
		"  --suspend-symbol <symbol>        "
		    "Custom UTF-8 symbol character to display in the indicator for the \"suspend\" action. Default is .\n"
		"  --hibernate-command <command>    "
		    "Command to run when \"hibernate\" action is activated.\n"
		"  --hibernate-label <label>        "
		    "Custom text label to display in the indicator for the \"hibernate\" action. Default is \"hibernate\".\n"
		"  --hibernate-symbol <symbol>      "
		    "Custom UTF-8 symbol character to display in the indicator for the \"hibernate\" action. Default is .\n"
		"  --logout-command <command>       "
		    "Command to run when \"logout\" action is activated.\n"
		"  --logout-label <label>           "
		    "Custom text label to display in the indicator for the \"logout\" action. Default is \"logout\".\n"
		"  --logout-symbol <symbol>         "
		    "Custom UTF-8 symbol character to display in the indicator for the \"logout\" action. Default is .\n"
		"  --reload-command <command>       "
		    "Command to run when \"reload wm config\" action is activated.\n"
		"  --reload-label <label>           "
		    "Custom text label to display in the indicator for the \"reload wm config\" action. Default is \"reload wm\".\n"
		"  --reload-symbol <symbol>         "
		    "Custom UTF-8 symbol character to display in the indicator for the \"reload wm\" action. Default is .\n"
		"  --lock-command <command>         "
		    "Command to run when \"lock\" action is activated.\n"
		"  --lock-label <label>             "
		    "Custom text label to display in the indicator for the \"lock\" action. Default is \"lock\".\n"
		"  --lock-symbol <symbol>           "
		    "Custom UTF-8 symbol character to display in the indicator for the \"lock\" action. Default is .\n"
		"  --switch-user-command <command>  "
		    "Command to run when \"switch user\" action is activated.\n"
		"  --switch-user-label <label>      "
		    "Custom text label to display in the indicator for the \"switch user\" action. Default is \"switch user\".\n"
		"  --switch-user-symbol <symbol>    "
		    "Custom UTF-8 symbol character to display in the indicator for the \"switch user\" action. Default is .\n"
		"  --cancel-label <label>           "
		    "Custom text label to display in the indicator for the \"cancel\" action. Default is \"cancel\".\n"
		"  --cancel-symbol <symbol>    "
		    "Custom UTF-8 symbol character to display in the indicator for the \"cancel\" action. Default is .\n"
		"  --default-action <action-name>   "
		    "Action to pre-select on start.\n"
		"  --hide-cancel                    "
			"Hide the indicator for the \"cancel\" option.\n"
		"  --reverse-arrows                 "
			"Reverse the direction of up/down arrows.\n"
		"  --scroll-sensitivity <amount>    "
		    "How fast selected action will change when scrolling with mouse/touch. "
			"Lower is faster; default is 8.\n"
		"  --instant-run                    "
			"Instantly run actions on key press, without confirmation with enter key.\n"
		"\n"
		"All <color> options are of the form <rrggbb[aa]>.\n";

	int c;
	optind = 1;
	while (1) {
		int opt_idx = 0;
		c = getopt_long(argc, argv, "c:dhi:Slnrs:tTvC:R:", long_options,
				&opt_idx);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'C':
			if (config_path) {
				*config_path = strdup(optarg);
			}
			break;
		case 'c':
			if (state) {
				state->args.colors.background = parse_color(optarg);
			}
			break;
		case 'd':
			waylogout_log_init(LOG_DEBUG);
			break;
		case 't':
			waylogout_log_init(LOG_TRACE);
			break;
		case 'i':
			if (state) {
				load_image(optarg, state);
			}
			break;
		case 'l':
			if (state) {
				state->args.labels = true;
			}
			break;
		case 'S':
			if (state) {
				state->args.screenshots = true;
			}
			break;
		case 'n':
			if (line_mode) {
				*line_mode = LM_INSIDE;
			}
			break;
		case 'r':
			if (line_mode) {
				*line_mode = LM_RING;
			}
			break;
		case 'R':
			if (state) {
				state->args.rows = atoi(optarg);
			}
			break;
		case 's':
			if (state) {
				state->args.mode = parse_background_mode(optarg);
				if (state->args.mode == BACKGROUND_MODE_INVALID) {
					return 1;
				}
			}
			break;
		case 'T':
			if (state) {
				state->args.mode = BACKGROUND_MODE_TILE;
			}
			break;
		case 'v':
			fprintf(stdout, "waylogout version " WAYLOGOUT_VERSION "\n");
			exit(EXIT_SUCCESS);
			break;
		case LO_SELECTION_LABEL:
			if (state) {
				state->args.selection_label = true;
			}
			break;
		case LO_FONT:
			if (state) {
				free(state->args.font);
				state->args.font = strdup_noquotes(optarg);
			}
			break;
		case LO_FA_FONT:
			if (state) {
				free(state->args.fa_font);
				state->args.fa_font = strdup_noquotes(optarg);
			}
			break;
		case LO_DEFAULT_ACTION:
			if (state) {
				free(state->args.default_action);
				state->args.default_action = strdup(optarg);
			}
			break;
		case LO_SYMBOL_FONT_SIZE:
			if (state) {
				state->args.symbol_font_size = atoi(optarg);
			}
			break;
		case LO_LABEL_FONT_SIZE:
			if (state) {
				state->args.label_font_size = atoi(optarg);
			}
			break;
		case LO_IND_RADIUS:
			if (state) {
				state->args.radius = strtol(optarg, NULL, 0);
			}
			break;
		case LO_IND_THICKNESS:
			if (state) {
				state->args.thickness = strtol(optarg, NULL, 0);
			}
			break;
		case LO_IND_X_POSITION:
			if (state) {
				state->args.override_indicator_x_position = true;
				state->args.indicator_x_position = atoi(optarg);
			}
			break;
		case LO_IND_Y_POSITION:
			if (state) {
				state->args.override_indicator_y_position = true;
				state->args.indicator_y_position = atoi(optarg);
			}
			break;
		case LO_IND_SEP:
			if (state) {
				state->args.indicator_sep = atoi(optarg);
			}
			break;
		case LO_INSIDE_COLOR:
			if (state) {
				state->args.colors.inside.normal = parse_color(optarg);
			}
			break;
		case LO_INSIDE_HL_COLOR:
			if (state) {
				state->args.colors.inside.selected = parse_color(optarg);
			}
			break;
		case LO_LINE_COLOR:
			if (state) {
				state->args.colors.line.normal = parse_color(optarg);
			}
			break;
		case LO_LINE_HL_COLOR:
			if (state) {
				state->args.colors.line.selected = parse_color(optarg);
			}
			break;
		case LO_RING_COLOR:
			if (state) {
				state->args.colors.ring.normal = parse_color(optarg);
			}
			break;
		case LO_RING_HL_COLOR:
			if (state) {
				state->args.colors.ring.selected = parse_color(optarg);
			}
			break;
		case LO_TEXT_COLOR:
			if (state) {
				state->args.colors.text.normal = parse_color(optarg);
			}
			break;
		case LO_TEXT_HL_COLOR:
			if (state) {
				state->args.colors.text.selected = parse_color(optarg);
			}
			break;
		case LO_EFFECT_BLUR:
			if (state) {
				state->args.effects = realloc(state->args.effects,
						sizeof(*state->args.effects) * ++state->args.effects_count);
				struct waylogout_effect *effect = &state->args.effects[state->args.effects_count - 1];
				effect->tag = EFFECT_BLUR;
				if (sscanf(optarg, "%dx%d", &effect->e.blur.radius, &effect->e.blur.times) != 2) {
					waylogout_log(LOG_ERROR, "Invalid blur effect argument %s, ignoring", optarg);
					state->args.effects_count -= 1;
				}
			}
			break;
		case LO_EFFECT_PIXELATE:
			if (state) {
				state->args.effects = realloc(state->args.effects,
						sizeof(*state->args.effects) * ++state->args.effects_count);
				struct waylogout_effect *effect = &state->args.effects[state->args.effects_count - 1];
				effect->tag = EFFECT_PIXELATE;
				effect->e.pixelate.factor = atoi(optarg);
			}
			break;
		case LO_EFFECT_SCALE:
			if (state) {
				state->args.effects = realloc(state->args.effects,
						sizeof(*state->args.effects) * ++state->args.effects_count);
				struct waylogout_effect *effect = &state->args.effects[state->args.effects_count - 1];
				effect->tag = EFFECT_SCALE;
				if (sscanf(optarg, "%lf", &effect->e.scale) != 1) {
					waylogout_log(LOG_ERROR, "Invalid scale effect argument %s, ignoring", optarg);
					state->args.effects_count -= 1;
				}
			}
			break;
		case LO_EFFECT_GREYSCALE:
			if (state) {
				state->args.effects = realloc(state->args.effects,
						sizeof(*state->args.effects) * ++state->args.effects_count);
				struct waylogout_effect *effect = &state->args.effects[state->args.effects_count - 1];
				effect->tag = EFFECT_GREYSCALE;
			}
			break;
		case LO_EFFECT_VIGNETTE:
			if (state) {
				state->args.effects = realloc(state->args.effects,
						sizeof(*state->args.effects) * ++state->args.effects_count);
				struct waylogout_effect *effect = &state->args.effects[state->args.effects_count - 1];
				effect->tag = EFFECT_VIGNETTE;
				if (sscanf(optarg, "%lf:%lf", &effect->e.vignette.base, &effect->e.vignette.factor) != 2) {
					waylogout_log(LOG_ERROR, "Invalid factor effect argument %s, ignoring", optarg);
					state->args.effects_count -= 1;
				}
			}
			break;
		case LO_EFFECT_COMPOSE:
			if (state) {
				state->args.effects = realloc(state->args.effects,
						sizeof(*state->args.effects) * ++state->args.effects_count);
				struct waylogout_effect *effect = &state->args.effects[state->args.effects_count - 1];
				effect->tag = EFFECT_COMPOSE;
				parse_effect_compose(optarg, effect);
			}
			break;
		case LO_EFFECT_CUSTOM:
			if (state) {
				state->args.effects = realloc(state->args.effects,
						sizeof(*state->args.effects) * ++state->args.effects_count);
				struct waylogout_effect *effect = &state->args.effects[state->args.effects_count - 1];
				effect->tag = EFFECT_CUSTOM;
				effect->e.custom = strdup(optarg);
			}
			break;
		case LO_TIME_EFFECTS:
			if (state) {
				state->args.time_effects = true;
			}
			break;
		case LO_FADE_IN:
			if (state) {
				state->args.fade_in = parse_seconds(optarg);
			}
			break;
		case LO_COMMAND_POWEROFF:
			if (state)
				add_action_command(state, WL_ACTION_POWEROFF, optarg);
			break;
		case LO_LABEL_POWEROFF:
			if (state)
				add_action_label(state, WL_ACTION_POWEROFF, optarg);
			break;
		case LO_SYMBOL_POWEROFF:
			if (state)
				add_action_symbol(state, WL_ACTION_POWEROFF, optarg);
			break;
		// TODO LO_SHORTCUT_POWEROFF
		case LO_COMMAND_REBOOT:
			if (state)
				add_action_command(state, WL_ACTION_REBOOT, optarg);
			break;
		case LO_LABEL_REBOOT:
			if (state)
				add_action_label(state, WL_ACTION_REBOOT, optarg);
			break;
		case LO_SYMBOL_REBOOT:
			if (state)
				add_action_symbol(state, WL_ACTION_REBOOT, optarg);
			break;
		// TODO LO_SHORTCUT_REBOOT
		case LO_COMMAND_SUSPEND:
			if (state)
				add_action_command(state, WL_ACTION_SUSPEND, optarg);
			break;
		case LO_LABEL_SUSPEND:
			if (state)
				add_action_label(state, WL_ACTION_SUSPEND, optarg);
			break;
		case LO_SYMBOL_SUSPEND:
			if (state)
				add_action_symbol(state, WL_ACTION_SUSPEND, optarg);
			break;
		// TODO LO_SHORTCUT_SUSPEND
		case LO_COMMAND_HIBERNATE:
			if (state)
				add_action_command(state, WL_ACTION_HIBERNATE, optarg);
			break;
		case LO_LABEL_HIBERNATE:
			if (state)
				add_action_label(state, WL_ACTION_HIBERNATE, optarg);
			break;
		case LO_SYMBOL_HIBERNATE:
			if (state)
				add_action_symbol(state, WL_ACTION_HIBERNATE, optarg);
			break;
		// TODO LO_SHORTCUT_HIBERNATE
		case LO_COMMAND_LOGOUT:
			if (state)
				add_action_command(state, WL_ACTION_LOGOUT, optarg);
			break;
		case LO_LABEL_LOGOUT:
			if (state)
				add_action_label(state, WL_ACTION_LOGOUT, optarg);
			break;
		case LO_SYMBOL_LOGOUT:
			if (state)
				add_action_symbol(state, WL_ACTION_LOGOUT, optarg);
			break;
		// TODO LO_SHORTCUT_LOGOUT
		case LO_COMMAND_RELOAD:
			if (state)
				add_action_command(state, WL_ACTION_RELOAD, optarg);
			break;
		case LO_LABEL_RELOAD:
			if (state)
				add_action_label(state, WL_ACTION_RELOAD, optarg);
			break;
		case LO_SYMBOL_RELOAD:
			if (state)
				add_action_symbol(state, WL_ACTION_RELOAD, optarg);
			break;
		// TODO LO_SHORTCUT_RELOAD
		case LO_COMMAND_LOCK:
			if (state)
				add_action_command(state, WL_ACTION_LOCK, optarg);
			break;
		case LO_LABEL_LOCK:
			if (state)
				add_action_label(state, WL_ACTION_LOCK, optarg);
			break;
		case LO_SYMBOL_LOCK:
			if (state)
				add_action_symbol(state, WL_ACTION_LOCK, optarg);
			break;
		// TODO LO_SHORTCUT_LOCK
		case LO_COMMAND_SWITCH:
			if (state)
				add_action_command(state, WL_ACTION_SWITCH, optarg);
			break;
		case LO_LABEL_SWITCH:
			if (state)
				add_action_label(state, WL_ACTION_SWITCH, optarg);
			break;
		case LO_SYMBOL_SWITCH:
			if (state)
				add_action_symbol(state, WL_ACTION_SWITCH, optarg);
			break;
		// TODO LO_SHORTCUT_SWITCH
		case LO_LABEL_CANCEL:
			if (state)
				add_action_label(state, WL_ACTION_CANCEL, optarg);
			break;
		case LO_SYMBOL_CANCEL:
			if (state)
				add_action_symbol(state, WL_ACTION_CANCEL, optarg);
			break;
		case LO_HIDE_CANCEL:
			if (state)
				state->args.hide_cancel = true;
			break;
		case LO_REVERSE_ARROWS:
			if (state)
				state->args.reverse_arrows = true;
			break;
		case LO_SCROLL_SENSITIVITY:
			if (state)
				state->args.scroll_sensitivity = atoi(optarg);
			break;
		case LO_INSTANT_RUN:
			if (state)
				state->args.instant_run = true;
			break;
		default:
			fprintf(stderr, "%s", usage);
			return 1;
		}
	}

	return 0;
}

static bool file_exists(const char *path) {
	return path && access(path, R_OK) != -1;
}

static char *get_config_path(void) {
	static const char *config_paths[] = {
		"$HOME/.waylogout/config",
		"$XDG_CONFIG_HOME/waylogout/config",
		SYSCONFDIR "/waylogout/config",
	};

	char *config_home = getenv("XDG_CONFIG_HOME");
	if (!config_home || config_home[0] == '\0') {
		config_paths[1] = "$HOME/.config/waylogout/config";
	}

	wordexp_t p;
	char *path;
	for (size_t i = 0; i < sizeof(config_paths) / sizeof(char *); ++i) {
		if (wordexp(config_paths[i], &p, 0) == 0) {
			path = strdup(p.we_wordv[0]);
			wordfree(&p);
			if (file_exists(path)) {
				return path;
			}
			free(path);
		}
	}

	return NULL;
}

static int load_config(char *path, struct waylogout_state *state,
		enum line_mode *line_mode) {
	FILE *config = fopen(path, "r");
	if (!config) {
		waylogout_log(LOG_ERROR, "Failed to read config. Running without it.");
		return 0;
	}
	char *line = NULL;
	size_t line_size = 0;
	ssize_t nread;
	int line_number = 0;
	int result = 0;
	while ((nread = getline(&line, &line_size, config)) != -1) {
		line_number++;

		if (line[nread - 1] == '\n') {
			line[--nread] = '\0';
		}

		if (!*line || line[0] == '#') {
			continue;
		}

		waylogout_log(LOG_DEBUG, "Config Line #%d: %s", line_number, line);
		char *flag = malloc(nread + 3);
		if (flag == NULL) {
			free(line);
			fclose(config);
			waylogout_log(LOG_ERROR, "Failed to allocate memory");
			return 0;
		}
		sprintf(flag, "--%s", line);
		char *argv[] = {"waylogout", flag};
		result = parse_options(2, argv, state, line_mode, NULL);
		free(flag);
		if (result != 0) {
			break;
		}
	}
	free(line);
	fclose(config);
	return 0;
}

static struct waylogout_state state;

static void display_in(int fd, short mask, void *data) {
	if (wl_display_dispatch(state.display) == -1) {
		state.run_display = false;
	}
}

static void end_allow_fade_period(void *data) {
	struct waylogout_state *state = data;
	if (state->args.allow_fade) {
		state->args.allow_fade = false;
	}
}

static void timer_render(void *data) {
	struct waylogout_state *state = (struct waylogout_state *)data;
	damage_state(state);
	loop_add_timer(state->eventloop, 1000, timer_render, state);
}

int main(int argc, char **argv) {
	waylogout_log_init(LOG_ERROR);
	srand(time(NULL));
	enum line_mode line_mode = LM_LINE;
	state.selected_action_depressed = false;
	state.run_action_now = false;
	state.args = (struct waylogout_args){
		.mode = BACKGROUND_MODE_FILL,
		.font = strdup("sans-serif"),
		.fa_font = strdup("Font Awesome 6 Free"),
		.default_action = NULL,
		.symbol_font_size = 0,
		.label_font_size = 0,
		.radius = 75,
		.thickness = 10,
		.indicator_x_position = 0,
		.indicator_y_position = 0,
		.indicator_sep = 0,
		.override_indicator_x_position = false,
		.override_indicator_y_position = false,
		.scroll_sensitivity = 8,
		.instant_run = false,
		.labels = false,
		.selection_label = false,
		.hide_cancel = false,
		.reverse_arrows = false,
		.screenshots = false,
		.rows = 1,
		.effects = NULL,
		.effects_count = 0,
		.allow_fade = true,
	};
	wl_list_init(&state.images);
	set_default_colors(&state.args.colors);

	state.selected_action = NULL;
	state.hovered_surface = NULL;
	state.touch.action = NULL;
	state.touch.id = 0;
	state.scroll_amount = 0;
	wl_list_init(&state.actions);

	char *config_path = NULL;
	int result = parse_options(argc, argv, NULL, NULL, &config_path);
	if (result != 0) {
		free(config_path);
		return result;
	}
	if (!config_path) {
		config_path = get_config_path();
	}

	if (config_path) {
		waylogout_log(LOG_DEBUG, "Found config at %s", config_path);
		int config_status = load_config(config_path, &state, &line_mode);
		free(config_path);
		if (config_status != 0) {
			free(state.args.font);
			return config_status;
		}
	}

	if (argc > 1) {
		waylogout_log(LOG_DEBUG, "Parsing CLI Args");
		int result = parse_options(argc, argv, &state, &line_mode, NULL);
		if (result != 0) {
			free(state.args.font);
			return result;
		}
	}

	struct waylogout_action *action;
	bool cancel_found = false;
	wl_list_for_each(action, &state.actions, link) {
		if (action->type == WL_ACTION_CANCEL) {
			cancel_found = true;
			break;
		}
	}
	if (cancel_found && state.args.hide_cancel) {
		waylogout_log(LOG_ERROR, "Label or symbol for cancel action configured, "
				"but hide-cancel option also specified.");
		return EXIT_FAILURE;
	}
	if (!cancel_found && !state.args.hide_cancel)
		add_action(&state, WL_ACTION_CANCEL, "cancel", "", NULL, XKB_KEY_c);

	state.n_actions = wl_list_length(&state.actions);
	if (state.n_actions - (!state.args.hide_cancel) < 1) {
		waylogout_log(LOG_ERROR, "No action commands configured --- "
				"no point running if user's only option is to do nothing.");
		return EXIT_FAILURE;
	}

	default_symbols[WL_ACTION_NO_ACTION][0] = '\0';
	strncpy(default_symbols[WL_ACTION_POWEROFF], "", 4);
	strncpy(default_symbols[WL_ACTION_REBOOT], "", 4);
	strncpy(default_symbols[WL_ACTION_SUSPEND], "", 4);
	strncpy(default_symbols[WL_ACTION_HIBERNATE], "", 4);
	strncpy(default_symbols[WL_ACTION_LOGOUT], "", 4);
	strncpy(default_symbols[WL_ACTION_RELOAD], "", 4);
	strncpy(default_symbols[WL_ACTION_LOCK], "", 4);
	strncpy(default_symbols[WL_ACTION_SWITCH], "", 4);
	strncpy(default_symbols[WL_ACTION_CANCEL], "", 4);

	wl_list_for_each(action, &state.actions, link) {
		if (!action->command && action->type != WL_ACTION_CANCEL) {
			if (action->label)
				waylogout_log(LOG_ERROR,
						"Label configured but no command configured for action \"%s\".",
						default_labels[action->type]);
			else if (action->symbol[0] != '\0')
				waylogout_log(LOG_ERROR,
						"Symbol configured but no command configured for action \"%s\".",
						default_labels[action->type]);
			else if (action->shortcut == XKB_KEY_VoidSymbol)
				waylogout_log(LOG_ERROR,
						"Keyboard shortcut configured but no command configured for action \"%s\".",
						default_labels[action->type]);
			return EXIT_FAILURE;
		}
		if (! action->label)
			action->label = strdup(default_labels[action->type]);
		if (action->symbol[0] == '\0')
			strncpy(action->symbol, default_symbols[action->type], 4);
		if (action->shortcut == XKB_KEY_VoidSymbol)
			action->shortcut = default_shortcuts[action->type];
		waylogout_log(LOG_DEBUG,
		  "Action %s:  \n"
		  "  symbol  %s\n"
		  "  command %s"
		  ,
		  action->label,
		  action->symbol,
		  action->command
		);
	}

	waylogout_log(LOG_DEBUG, "Found %d configured actions", state.n_actions);

	setup_rows(&state);
	set_default_action(&state);

	state.args.scroll_sensitivity = state.args.scroll_sensitivity * 1000;

	if (line_mode == LM_INSIDE) {
		state.args.colors.line = state.args.colors.inside;
	} else if (line_mode == LM_RING) {
		state.args.colors.line = state.args.colors.ring;
	}

	wl_list_init(&state.surfaces);
	state.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	state.display = wl_display_connect(NULL);
	if (!state.display) {
		free(state.args.font);
		waylogout_log(LOG_ERROR, "Unable to connect to the compositor. "
				"If your compositor is running, check or set the "
				"WAYLAND_DISPLAY environment variable.");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	if (!state.compositor) {
		waylogout_log(LOG_ERROR, "Missing wl_compositor");
		return 1;
	}

	if (!state.subcompositor) {
		waylogout_log(LOG_ERROR, "Missing wl_subcompositor");
		return 1;
	}

	if (!state.shm) {
		waylogout_log(LOG_ERROR, "Missing wl_shm");
		return 1;
	}

	struct waylogout_surface *surface;
	// Enumerate all outputs so that screenshots can be obtained
	wl_list_for_each(surface, &state.surfaces, link) {
		surface->events_pending += 1;
	};

	wl_list_for_each(surface, &state.surfaces, link) {
		while (surface->events_pending > 0) {
			wl_display_roundtrip(state.display);
		}
	}

	// Apply effects to all images
	struct waylogout_image *iter_image, *temp;
	wl_list_for_each_safe(iter_image, temp, &state.images, link) {
		iter_image->cairo_surface = apply_effects(
				iter_image->cairo_surface, &state, 1);
	}

	if (wl_display_roundtrip(state.display) == -1) {
		free(state.args.font);
		return 1;
	}

	wl_list_for_each(surface, &state.surfaces, link) {
		create_surface(surface);
	}

	wl_list_for_each(surface, &state.surfaces, link) {
		while (surface->events_pending > 0) {
			wl_display_roundtrip(state.display);
		}
	}

	create_cursor_surface(&state);

	state.eventloop = loop_create();
	loop_add_fd(state.eventloop, wl_display_get_fd(state.display), POLLIN,
			display_in, NULL);

	loop_add_timer(state.eventloop, 1000, timer_render, &state);

	if (state.args.fade_in) {
		loop_add_timer(state.eventloop, state.args.fade_in, end_allow_fade_period, &state);
	}

	// Re-draw once to start the draw loop
	damage_state(&state);

	state.run_display = true;
	while (state.run_display) {
		errno = 0;
		if (wl_display_flush(state.display) == -1 && errno != EAGAIN) {
			break;
		}
		if (state.selected_action && state.run_action_now && state.selected_action->rendered_depressed) {
			/* 0.2s delay */
			struct timespec ts;
			ts.tv_sec = 0;
			ts.tv_nsec = 200000000;
			nanosleep(&ts, NULL);
			run_action(state.selected_action);
		}
		loop_poll(state.eventloop);
	}

	free(state.args.font);
	return 0;
}
