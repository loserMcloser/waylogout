#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
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
#include <wordexp.h>
#include "background-image.h"
#include "cairo.h"
#include "log.h"
#include "loop.h"
#include "pool-buffer.h"
#include "seat.h"
#include "waylogout.h"
#include "wlr-input-inhibitor-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

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

// TODO might not need this
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

// TODO might not need this
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
	destroy_buffer(&surface->indicator_buffers[0]);
	destroy_buffer(&surface->indicator_buffers[1]);
	fade_destroy(&surface->fade);
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

struct zxdg_output_v1_listener _xdg_output_listener;

static void create_layer_surface(struct waylogout_surface *surface) {
	struct waylogout_state *state = surface->state;

	if (state->args.fade_in) {
		surface->fade.target_time = state->args.fade_in;
	}

	surface->image = select_image(state, surface);

	static bool has_printed_zxdg_error = false;
	if (state->zxdg_output_manager) {
		surface->xdg_output = zxdg_output_manager_v1_get_xdg_output(
				state->zxdg_output_manager, surface->output);
		zxdg_output_v1_add_listener(
				surface->xdg_output, &_xdg_output_listener, surface);
		surface->events_pending += 1;
	} else if (!has_printed_zxdg_error) {
		waylogout_log(LOG_INFO, "Compositor does not support zxdg output "
				"manager, images assigned to named outputs will not work");
		has_printed_zxdg_error = true;
	}

	surface->surface = wl_compositor_create_surface(state->compositor);
	assert(surface->surface);

	surface->child = wl_compositor_create_surface(state->compositor);
	assert(surface->child);
	surface->subsurface = wl_subcompositor_get_subsurface(state->subcompositor, surface->child, surface->surface);
	assert(surface->subsurface);
	wl_subsurface_set_sync(surface->subsurface);

	surface->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			state->layer_shell, surface->surface, surface->output,
			ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "lockscreen");
	assert(surface->layer_surface);

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

	render_frame_background(surface);
	render_background_fade_prepare(surface, surface->current_buffer);
	render_frame(surface);
}

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	surface->width = width;
	surface->height = height;
	surface->indicator_width = 1;
	surface->indicator_height = 1;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	if (--surface->events_pending == 0) {
		initially_render_surface(surface);
	}
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

		render_frame(surface);
	}
}

static const struct wl_callback_listener surface_frame_listener = {
	.done = surface_frame_handle_done,
};

void damage_surface(struct waylogout_surface *surface) {
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

static void handle_wl_output_done(void *data, struct wl_output *output) {
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

struct wl_output_listener _wl_output_listener = {
	.geometry = handle_wl_output_geometry,
	.mode = handle_wl_output_mode,
	.done = handle_wl_output_done,
	.scale = handle_wl_output_scale,
};

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
	} else  {
		surface->screencopy.image->cairo_surface =
			apply_effects(image, state, surface->scale);
		surface->image = surface->screencopy.image->cairo_surface;
	}

	waylogout_log(LOG_DEBUG, "Loaded screenshot for output %s", surface->output_name);
	wl_list_insert(&state->images, &surface->screencopy.image->link);
	if (--surface->events_pending == 0) {
		initially_render_surface(surface);
	}
}

static void handle_screencopy_frame_failed(void *data,
		struct zwlr_screencopy_frame_v1 *frame) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	waylogout_log(LOG_ERROR, "Screencopy failed");

	if (--surface->events_pending == 0) {
		initially_render_surface(surface);
	}
}

static const struct zwlr_screencopy_frame_v1_listener screencopy_frame_listener = {
	.buffer = handle_screencopy_frame_buffer,
	.flags = handle_screencopy_frame_flags,
	.ready = handle_screencopy_frame_ready,
	.failed = handle_screencopy_frame_failed,
};

static void handle_xdg_output_logical_size(void *data, struct zxdg_output_v1 *output,
		int width, int height) {
	// Who cares
}

static void handle_xdg_output_logical_position(void *data,
		struct zxdg_output_v1 *output, int x, int y) {
	// Who cares
}

static void handle_xdg_output_name(void *data, struct zxdg_output_v1 *output,
		const char *name) {
	waylogout_trace();
	waylogout_log(LOG_DEBUG, "output name is %s", name);
	struct waylogout_surface *surface = data;
	surface->xdg_output = output;
	surface->output_name = strdup(name);
}

static void handle_xdg_output_description(void *data, struct zxdg_output_v1 *output,
		const char *description) {
	// Who cares
}

static void handle_xdg_output_done(void *data, struct zxdg_output_v1 *output) {
	waylogout_trace();
	struct waylogout_surface *surface = data;
	struct waylogout_state *state = surface->state;
	cairo_surface_t *new_image = select_image(surface->state, surface);

	if (new_image == surface->image && state->args.screenshots) {
		static bool has_printed_screencopy_error = false;
		if (state->screencopy_manager) {
			surface->screencopy_frame = zwlr_screencopy_manager_v1_capture_output(
					state->screencopy_manager, false, surface->output);
			zwlr_screencopy_frame_v1_add_listener(surface->screencopy_frame,
					&screencopy_frame_listener, surface);
			surface->events_pending += 1;
		} else if (!has_printed_screencopy_error) {
			waylogout_log(LOG_INFO, "Compositor does not support screencopy manager, "
					"screenshots will not work");
			has_printed_screencopy_error = true;
		}
	} else if (new_image != NULL) {
		if (state->args.screenshots) {
			waylogout_log(LOG_DEBUG,
					"Using existing image instead of taking a screenshot for output %s.",
					surface->output_name);
		}
		surface->image = new_image;
	}

	if (--surface->events_pending == 0) {
		initially_render_surface(surface);
	}
}

struct zxdg_output_v1_listener _xdg_output_listener = {
	.logical_position = handle_xdg_output_logical_position,
	.logical_size = handle_xdg_output_logical_size,
	.done = handle_xdg_output_done,
	.name = handle_xdg_output_name,
	.description = handle_xdg_output_description,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {

	struct waylogout_state *state = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, 3);
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
	} else if (strcmp(interface, zwlr_input_inhibit_manager_v1_interface.name) == 0) {
		state->input_inhibit_manager = wl_registry_bind(
				registry, name, &zwlr_input_inhibit_manager_v1_interface, 1);
	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		state->zxdg_output_manager = wl_registry_bind(
				registry, name, &zxdg_output_manager_v1_interface, 2);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct waylogout_surface *surface =
			calloc(1, sizeof(struct waylogout_surface));
		surface->state = state;
		surface->output = wl_registry_bind(registry, name,
				&wl_output_interface, 3);
		surface->output_global_name = name;
		wl_output_add_listener(surface->output, &_wl_output_listener, surface);
		wl_list_insert(&state->surfaces, &surface->link);

		if (state->run_display) {
			create_layer_surface(surface);
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

static void load_image(char *arg, struct waylogout_state *state) {
	// [[<output>]:]<path>
	struct waylogout_image *image = calloc(1, sizeof(struct waylogout_image));
	char *separator = strchr(arg, ':');
	if (separator) {
		*separator = '\0';
		image->output_name = separator == arg ? NULL : strdup(arg);
		image->path = strdup(separator + 1);
	} else {
		image->output_name = NULL;
		image->path = strdup(arg);
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

// TODO edit/prune these
static void set_default_colors(struct waylogout_colors *colors) {
	colors->background = 0xFFFFFFFF;
	colors->bs_highlight = 0xDB3300FF;
	colors->key_highlight = 0x33DB00FF;
	colors->caps_lock_bs_highlight = 0xDB3300FF;
	colors->caps_lock_key_highlight = 0x33DB00FF;
	colors->separator = 0x000000FF;
	colors->inside = (struct waylogout_colorset){
		.normal = 0x000000C0,
		.highlight = 0xFA0000C0,
	};
	colors->line = (struct waylogout_colorset){
		.normal = 0x000000FF,
		.highlight = 0x000000FF,
	};
	colors->ring = (struct waylogout_colorset){
		.normal = 0x337D00FF,
		.highlight = 0x7D3300FF,
	};
	colors->text = (struct waylogout_colorset){
		.normal = 0xE5A445FF,
		.highlight = 0x000000FF,
	};
}

enum line_mode {
	LM_LINE,
	LM_INSIDE,
	LM_RING,
};

static void fill_action(struct waylogout_state *state,
	enum waylogout_actions action, char *label, char *symbol, char *command,
	xkb_keysym_t shortcut) {
	state->actions[action] = (struct waylogout_action){
		.show = true,
		.selected = false,
		.label = strdup(label),
		.shortcut = shortcut,
	};
	strncpy(state->actions[action].symbol, symbol, 4);
	if (command)
		state->actions[action].command = strdup(command);
	waylogout_log(LOG_DEBUG,
	  "Action %s:  \n"
	  "  symbol  %s\n"
	  "  command %s  "
	  ,
	  state->actions[action].label,
	  state->actions[action].symbol,
	  state->actions[action].command
	);
}

// TODO lots of editting/pruning to do here
static int parse_options(int argc, char **argv, struct waylogout_state *state,
		enum line_mode *line_mode, char **config_path) {
	enum long_option_codes {
		LO_TRACE,
		LO_BS_HL_COLOR = 256,
		LO_CAPS_LOCK_BS_HL_COLOR,
		LO_CAPS_LOCK_KEY_HL_COLOR,
		LO_FONT,
		LO_FONT_SIZE,
		LO_IND_RADIUS,
		LO_IND_X_POSITION,
		LO_IND_Y_POSITION,
		LO_IND_THICKNESS,
		LO_INSIDE_COLOR,
		LO_INSIDE_HIGHLIGHT_COLOR,
		LO_KEY_HL_COLOR,
		LO_LAYOUT_TXT_COLOR,
		LO_LAYOUT_BG_COLOR,
		LO_LAYOUT_BORDER_COLOR,
		LO_LINE_COLOR,
		LO_LINE_HIGHLIGHT_COLOR,
		LO_RING_COLOR,
		LO_RING_HIGHLIGHT_COLOR,
		LO_SEP_COLOR,
		LO_TEXT_COLOR,
		LO_TEXT_HIGHLIGHT_COLOR,
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
		LO_HIDE_CANCEL,
		LO_COMMAND_POWEROFF,
		LO_COMMAND_REBOOT,
		LO_COMMAND_SUSPEND,
		LO_COMMAND_HIBERNATE,
		LO_COMMAND_LOGOUT,
		LO_COMMAND_LOCK,
		LO_COMMAND_SWITCH,
	};

	static struct option long_options[] = {
		{"config", required_argument, NULL, 'C'},
		{"color", required_argument, NULL, 'c'},
		{"debug", no_argument, NULL, 'd'},
		{"trace", no_argument, NULL, LO_TRACE},
		{"help", no_argument, NULL, 'h'},
		{"image", required_argument, NULL, 'i'},
		{"labels", no_argument, NULL, 'l'},
		{"line-uses-inside", no_argument, NULL, 'n'},
		{"line-uses-ring", no_argument, NULL, 'r'},
		{"screenshots", no_argument, NULL, 'S'},
		{"scaling", required_argument, NULL, 's'},
		{"tiling", no_argument, NULL, 't'},
		{"version", no_argument, NULL, 'v'},
		{"bs-hl-color", required_argument, NULL, LO_BS_HL_COLOR},
		{"caps-lock-bs-hl-color", required_argument, NULL, LO_CAPS_LOCK_BS_HL_COLOR},
		{"caps-lock-key-hl-color", required_argument, NULL, LO_CAPS_LOCK_KEY_HL_COLOR},
		{"font", required_argument, NULL, LO_FONT},
		{"font-size", required_argument, NULL, LO_FONT_SIZE},
		{"indicator-radius", required_argument, NULL, LO_IND_RADIUS},
		{"indicator-thickness", required_argument, NULL, LO_IND_THICKNESS},
		{"indicator-x-position", required_argument, NULL, LO_IND_X_POSITION},
		{"indicator-y-position", required_argument, NULL, LO_IND_Y_POSITION},
		{"inside-color", required_argument, NULL, LO_INSIDE_COLOR},
		{"inside-highlight-color", required_argument, NULL, LO_INSIDE_HIGHLIGHT_COLOR},
		{"key-hl-color", required_argument, NULL, LO_KEY_HL_COLOR},
		{"layout-bg-color", required_argument, NULL, LO_LAYOUT_BG_COLOR},
		{"layout-border-color", required_argument, NULL, LO_LAYOUT_BORDER_COLOR},
		{"layout-text-color", required_argument, NULL, LO_LAYOUT_TXT_COLOR},
		{"line-color", required_argument, NULL, LO_LINE_COLOR},
		{"line-highlight-color", required_argument, NULL, LO_LINE_HIGHLIGHT_COLOR},
		{"ring-color", required_argument, NULL, LO_RING_COLOR},
		{"ring-highlight-color", required_argument, NULL, LO_RING_HIGHLIGHT_COLOR},
		{"separator-color", required_argument, NULL, LO_SEP_COLOR},
		{"text-color", required_argument, NULL, LO_TEXT_COLOR},
		{"text-highlight-color", required_argument, NULL, LO_TEXT_HIGHLIGHT_COLOR},
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
		{"lock-command", required_argument, NULL, LO_COMMAND_LOCK},
		{"switch-user-command", required_argument, NULL, LO_COMMAND_SWITCH},
		{"hide-cancel", no_argument, NULL, LO_HIDE_CANCEL},
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
		"  --fade-in <seconds>              "
			"Make the lock screen fade in instead of just popping in.\n"
		"  --submit-on-touch                "
			"Submit password in response to a touch event.\n"
		"  --grace <seconds>                "
			"Password grace period. Don't require the password for the first N seconds.\n"
		"  --grace-no-mouse                 "
			"During the grace period, don't unlock on a mouse event.\n"
		"  --grace-no-touch                 "
			"During the grace period, don't unlock on a touch event.\n"
		"  -h, --help                       "
			"Show help message and quit.\n"
		"  -i, --image [[<output>]:]<path>  "
			"Display the given image, optionally only on the given output.\n"
		"  -l, --labels                     "
			"Show action labels.\n"
		"  -S, --screenshots                "
			"Use a screenshots as the background image.\n"
		"  -s, --scaling <mode>             "
			"Image scaling mode: stretch, fill, fit, center, tile, solid_color.\n"
		"  -t, --tiling                     "
			"Same as --scaling=tile.\n"
		"  --user                           "
			"Show name of locked user.\n"
		"  -v, --version                    "
			"Show the version number and quit.\n"
		"  --bs-hl-color <color>            "
			"Sets the color of backspace highlight segments.\n"
		"  --caps-lock-bs-hl-color <color>  "
			"Sets the color of backspace highlight segments when Caps Lock "
			"is active.\n"
		"  --caps-lock-key-hl-color <color> "
			"Sets the color of the key press highlight segments when "
			"Caps Lock is active.\n"
		"  --font <font>                    "
			"Sets the font of the text.\n"
		"  --font-size <size>               "
			"Sets a fixed font size for the indicator text.\n"
		"  --hide-cancel                    "
			"Hide the indicator for the \"cancel\" option.\n"
		"  --indicator-radius <radius>      "
			"Sets the indicator radius.\n"
		"  --indicator-thickness <thick>    "
			"Sets the indicator thickness.\n"
		"  --indicator-x-position <x>       "
			"Sets the horizontal position of the indicator.\n"
		"  --indicator-y-position <y>       "
			"Sets the vertical position of the indicator.\n"
		"  --inside-color <color>           "
			"Sets the color of the inside of the indicator.\n"
		"  --inside-highlight-color <color>     "
			"Sets the color of the inside of the indicator when invalid.\n"
		"  --key-hl-color <color>           "
			"Sets the color of the key press highlight segments.\n"
		"  --layout-bg-color <color>        "
			"Sets the background color of the box containing the layout text.\n"
		"  --layout-border-color <color>    "
			"Sets the color of the border of the box containing the layout text.\n"
		"  --layout-text-color <color>      "
			"Sets the color of the layout text.\n"
		"  --line-color <color>             "
			"Sets the color of the line between the inside and ring.\n"
		"  --line-clear-color <color>       "
			"Sets the color of the line between the inside and ring when "
			"cleared.\n"
		"  --line-caps-lock-color <color>   "
			"Sets the color of the line between the inside and ring when "
			"Caps Lock is active.\n"
		"  --line-ver-color <color>         "
			"Sets the color of the line between the inside and ring when "
			"verifying.\n"
		"  --line-wrong-color <color>       "
			"Sets the color of the line between the inside and ring when "
			"invalid.\n"
		"  -n, --line-uses-inside           "
			"Use the inside color for the line between the inside and ring.\n"
		"  -r, --line-uses-ring             "
			"Use the ring color for the line between the inside and ring.\n"
		"  --ring-color <color>             "
			"Sets the color of the ring of the indicator.\n"
		"  --ring-clear-color <color>       "
			"Sets the color of the ring of the indicator when cleared.\n"
		"  --ring-caps-lock-color <color>   "
			"Sets the color of the ring of the indicator when Caps Lock "
			"is active.\n"
		"  --ring-ver-color <color>         "
			"Sets the color of the ring of the indicator when verifying.\n"
		"  --ring-wrong-color <color>       "
			"Sets the color of the ring of the indicator when invalid.\n"
		"  --separator-color <color>        "
			"Sets the color of the lines that separate highlight segments.\n"
		"  --text-color <color>             "
			"Sets the color of the text.\n"
		"  --text-clear-color <color>       "
			"Sets the color of the text when cleared.\n"
		"  --text-caps-lock-color <color>   "
			"Sets the color of the text when Caps Lock is active.\n"
		"  --text-ver-color <color>         "
			"Sets the color of the text when verifying.\n"
		"  --text-wrong-color <color>       "
			"Sets the color of the text when invalid.\n"
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
		"\n"
		"All <color> options are of the form <rrggbb[aa]>.\n";

	int c;
	optind = 1;
	while (1) {
		int opt_idx = 0;
		c = getopt_long(argc, argv, "c:dhi:lnrSs:tvC:", long_options,
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
		case LO_TRACE:
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
		case LO_BS_HL_COLOR:
			if (state) {
				state->args.colors.bs_highlight = parse_color(optarg);
			}
			break;
		case LO_CAPS_LOCK_BS_HL_COLOR:
			if (state) {
				state->args.colors.caps_lock_bs_highlight = parse_color(optarg);
			}
			break;
		case LO_CAPS_LOCK_KEY_HL_COLOR:
			if (state) {
				state->args.colors.caps_lock_key_highlight = parse_color(optarg);
			}
			break;
		case LO_FONT:
			if (state) {
				free(state->args.font);
				state->args.font = strdup(optarg);
			}
			break;
		case LO_FONT_SIZE:
			if (state) {
				state->args.font_size = atoi(optarg);
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
		case LO_INSIDE_COLOR:
			if (state) {
				state->args.colors.inside.normal = parse_color(optarg);
			}
			break;
		case LO_INSIDE_HIGHLIGHT_COLOR:
			if (state) {
				state->args.colors.inside.highlight = parse_color(optarg);
			}
			break;
		case LO_KEY_HL_COLOR:
			if (state) {
				state->args.colors.key_highlight = parse_color(optarg);
			}
			break;
		case LO_LAYOUT_BG_COLOR:
			if (state) {
				state->args.colors.layout_background = parse_color(optarg);
			}
			break;
		case LO_LAYOUT_BORDER_COLOR:
			if (state) {
				state->args.colors.layout_border = parse_color(optarg);
			}
			break;
		case LO_LAYOUT_TXT_COLOR:
			if (state) {
				state->args.colors.layout_text = parse_color(optarg);
			}
			break;
		case LO_LINE_COLOR:
			if (state) {
				state->args.colors.line.normal = parse_color(optarg);
			}
			break;
		case LO_LINE_HIGHLIGHT_COLOR:
			if (state) {
				state->args.colors.line.highlight = parse_color(optarg);
			}
			break;
		case LO_RING_COLOR:
			if (state) {
				state->args.colors.ring.normal = parse_color(optarg);
			}
			break;
		case LO_RING_HIGHLIGHT_COLOR:
			if (state) {
				state->args.colors.ring.highlight = parse_color(optarg);
			}
			break;
		case LO_SEP_COLOR:
			if (state) {
				state->args.colors.separator = parse_color(optarg);
			}
			break;
		case LO_TEXT_COLOR:
			if (state) {
				state->args.colors.text.normal = parse_color(optarg);
			}
			break;
		case LO_TEXT_HIGHLIGHT_COLOR:
			if (state) {
				state->args.colors.text.highlight = parse_color(optarg);
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
			if (state) {
				fill_action(
					state, ACTION_POWEROFF, "power off",
					"",
					optarg,
					XKB_KEY_p
				);
				++(state->n_actions);
			}
			break;
		case LO_COMMAND_REBOOT:
			if (state) {
				fill_action(
					state, ACTION_REBOOT, "reboot",
					"",
					optarg,
					XKB_KEY_r
				);
				++(state->n_actions);
			}
			break;
		case LO_COMMAND_SUSPEND:
			if (state) {
				fill_action(
					state, ACTION_SUSPEND, "sleep",
					"",
					optarg,
					XKB_KEY_s
				);
				++(state->n_actions);
			}
			break;
		case LO_COMMAND_HIBERNATE:
			if (state) {
				fill_action(
					state, ACTION_HIBERNATE, "hibernate",
					"",
					optarg,
					XKB_KEY_h
				);
				++(state->n_actions);
			}
			break;
		case LO_COMMAND_LOGOUT:
			if (state) {
				fill_action(
					state, ACTION_HIBERNATE, "logout",
					"",
					optarg,
					XKB_KEY_x
				);
				++(state->n_actions);
			}
			break;
		case LO_COMMAND_LOCK:
			if (state) {
				fill_action(
					state, ACTION_HIBERNATE, "lock",
					"",
					optarg,
					XKB_KEY_k
				);
				++(state->n_actions);
			}
			break;
		case LO_COMMAND_SWITCH:
			if (state) {
				fill_action(
					state, ACTION_HIBERNATE, "switch user",
					"",
					optarg,
					XKB_KEY_w
				);
				++(state->n_actions);
			}
			break;
		case LO_HIDE_CANCEL:
			if (state)
				state->args.hide_cancel = true;
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
			free(config);
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

static void timer_render(void *data) {
	struct waylogout_state *state = (struct waylogout_state *)data;
	damage_state(state);
	loop_add_timer(state->eventloop, 1000, timer_render, state);
}

int main(int argc, char **argv) {
	waylogout_log_init(LOG_ERROR);
	srand(time(NULL));
	enum line_mode line_mode = LM_LINE;
	state.args = (struct waylogout_args){
		.mode = BACKGROUND_MODE_FILL,
		.font = strdup("sans-serif"),
		.font_size = 0,
		.radius = 75,
		.thickness = 10,
		.indicator_x_position = 0,
		.indicator_y_position = 0,
		.override_indicator_x_position = false,
		.override_indicator_y_position = false,
		.labels = false,
		.hide_cancel = false,
		.screenshots = false,
		.effects = NULL,
		.effects_count = 0,
		.user = false,
	};
	state.selected_action = -1;
	state.n_actions = 0;
	for (int i = 0; i < N_WAYLOGOUT_ACTIONS; i++) {
		state.actions[i].show = false;
		state.actions[i].selected = false;
	}

	wl_list_init(&state.images);
	set_default_colors(&state.args.colors);

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

	if (!state.args.hide_cancel) {
		fill_action(&state, ACTION_CANCEL, "cancel", "", NULL, XKB_KEY_c);
		state.actions[ACTION_CANCEL].show = true;
		++state.n_actions;
	}

	int n_non_cancel_actions = state.n_actions - (!state.args.hide_cancel);
	if (n_non_cancel_actions < 1) {
		waylogout_log(LOG_ERROR, "No action commands configured --- "
				"no point running if user's only option is to do nothing.");
		return EXIT_FAILURE;
	}

	waylogout_log(LOG_DEBUG, "Found %d configured actions", state.n_actions);

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
	assert(state.compositor && state.layer_shell && state.shm);
	if (!state.input_inhibit_manager) {
		free(state.args.font);
		waylogout_log(LOG_ERROR, "Compositor does not support the input "
				"inhibitor protocol, refusing to run insecurely");
		return 1;
	}

	zwlr_input_inhibit_manager_v1_get_inhibitor(state.input_inhibit_manager);
	if (wl_display_roundtrip(state.display) == -1) {
		free(state.args.font);
		waylogout_log(LOG_ERROR, "Exiting - failed to inhibit input:"
				" is a lockscreen already running?");
		return 2;
	}

	// Need to apply effects to all images loaded with --image
	struct waylogout_image *iter_image, *temp;
	wl_list_for_each_safe(iter_image, temp, &state.images, link) {
		iter_image->cairo_surface = apply_effects(
				iter_image->cairo_surface, &state, 1);
	}

	struct waylogout_surface *surface;
	wl_list_for_each(surface, &state.surfaces, link) {
		create_layer_surface(surface);
	}

	wl_list_for_each(surface, &state.surfaces, link) {
		while (surface->events_pending > 0) {
			wl_display_roundtrip(state.display);
		}
	}

	state.eventloop = loop_create();
	loop_add_fd(state.eventloop, wl_display_get_fd(state.display), POLLIN,
			display_in, NULL);

	loop_add_timer(state.eventloop, 1000, timer_render, &state);

	// Re-draw once to start the draw loop
	damage_state(&state);

	state.run_display = true;
	while (state.run_display) {
		errno = 0;
		if (wl_display_flush(state.display) == -1 && errno != EAGAIN) {
			break;
		}
		loop_poll(state.eventloop);
	}

	free(state.args.font);
	return 0;
}
