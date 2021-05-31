#ifndef _WAYLOGOUT_H
#define _WAYLOGOUT_H
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include "background-image.h"
#include "cairo.h"
#include "pool-buffer.h"
#include "seat.h"
#include "effects.h"
#include "fade.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct waylogout_colorset {
	uint32_t normal;
	uint32_t highlight;
};

struct waylogout_colors {
	uint32_t background;
	uint32_t bs_highlight;
	uint32_t key_highlight;
	uint32_t caps_lock_bs_highlight;
	uint32_t caps_lock_key_highlight;
	uint32_t separator;
	uint32_t layout_background;
	uint32_t layout_border;
	uint32_t layout_text;
	struct waylogout_colorset inside;
	struct waylogout_colorset line;
	struct waylogout_colorset ring;
	struct waylogout_colorset text;
};

struct waylogout_args {
	struct waylogout_colors colors;
	enum background_mode mode;
	char *font;
	uint32_t font_size;
	uint32_t radius;
	uint32_t thickness;
	uint32_t indicator_x_position;
	uint32_t indicator_y_position;
	bool override_indicator_x_position;
	bool override_indicator_y_position;
	bool labels;
	bool hide_cancel;

	bool screenshots;
	struct waylogout_effect *effects;
	int effects_count;
	bool time_effects;
	bool user;
	uint32_t fade_in;
};

#define N_WAYLOGOUT_ACTIONS 7
enum waylogout_actions {
	ACTION_LOGOUT,
	ACTION_SUSPEND,
	ACTION_HIBERNATE,
	ACTION_POWEROFF,
	ACTION_REBOOT,
	ACTION_SWITCH,
	ACTION_CANCEL
};

struct waylogout_action {
	bool show;
	bool selected;
	char *label;
	char symbol[4];
	char *command;
	xkb_keysym_t shortcut;
};

struct waylogout_state {
	struct loop *eventloop;
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_subcompositor *subcompositor;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zwlr_input_inhibit_manager_v1 *input_inhibit_manager;
	struct zwlr_screencopy_manager_v1 *screencopy_manager;
	struct wl_shm *shm;
	struct wl_list surfaces;
	struct wl_list images;
	struct waylogout_args args;
	struct waylogout_action actions[N_WAYLOGOUT_ACTIONS];
	int n_actions;
	int selected_action;
	struct waylogout_xkb xkb;
	int render_randnum;
	size_t n_screenshots_done;
	bool run_display;
	struct zxdg_output_manager_v1 *zxdg_output_manager;
};

struct waylogout_surface {
	cairo_surface_t *image;
	struct {
		uint32_t format, width, height, stride;
		enum wl_output_transform transform;
		void *data;
		struct waylogout_image *image;
	} screencopy;
	struct waylogout_state *state;
	struct wl_output *output;
	uint32_t output_global_name;
	struct zxdg_output_v1 *xdg_output;
	struct wl_surface *surface;
	struct wl_surface *child; // surface made into subsurface
	struct wl_subsurface *subsurface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct zwlr_screencopy_frame_v1 *screencopy_frame;
	struct pool_buffer buffers[2];
	struct pool_buffer indicator_buffers[2];
	struct pool_buffer *current_buffer;
	struct waylogout_fade fade;
	int events_pending;
	bool frame_pending, dirty;
	uint32_t width, height;
	uint32_t indicator_width, indicator_height;
	int32_t scale;
	enum wl_output_subpixel subpixel;
	enum wl_output_transform transform;
	char *output_name;
	struct wl_list link;
};

// There is exactly one waylogout_image for each -i argument
struct waylogout_image {
	char *path;
	char *output_name;
	cairo_surface_t *cairo_surface;
	struct wl_list link;
};

void waylogout_handle_key(struct waylogout_state *state,
		xkb_keysym_t keysym, uint32_t codepoint);
void waylogout_handle_mouse(struct waylogout_state *state);
void waylogout_handle_touch(struct waylogout_state *state);
void render_frame_background(struct waylogout_surface *surface);
void render_background_fade(struct waylogout_surface *surface, uint32_t time);
void render_background_fade_prepare(struct waylogout_surface *surface, struct pool_buffer *buffer);
void render_frame(struct waylogout_surface *surface);
void render_frames(struct waylogout_state *state);
void damage_surface(struct waylogout_surface *surface);
void damage_state(struct waylogout_state *state);
void schedule_indicator_clear(struct waylogout_state *state);

#endif
