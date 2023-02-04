#ifndef _WAYLOGOUT_H
#define _WAYLOGOUT_H
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "background-image.h"
#include "cairo.h"
#include "pool-buffer.h"
#include "seat.h"
#include "effects.h"
#include "fade.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct waylogout_colorset {
	uint32_t normal;
	uint32_t selected;
};

struct waylogout_colors {
	uint32_t background;
	struct waylogout_colorset inside;
	struct waylogout_colorset line;
	struct waylogout_colorset ring;
	struct waylogout_colorset text;
};

struct waylogout_args {
	struct waylogout_colors colors;
	enum background_mode mode;
	char *font;
	char *fa_font;
	uint32_t symbol_font_size;
	uint32_t label_font_size;
	uint32_t radius;
	uint32_t thickness;
	uint32_t indicator_x_position;
	uint32_t indicator_y_position;
	uint32_t indicator_sep;
	uint32_t scroll_sensitivity;
	bool override_indicator_x_position;
	bool override_indicator_y_position;
	bool labels;
	bool selection_label;
	bool hide_cancel;
	bool reverse_arrows;
	bool screenshots;
	struct waylogout_effect *effects;
	int effects_count;
	bool time_effects;
	uint32_t fade_in;
};

struct waylogout_surface;

enum waylogout_action_type {
	WL_ACTION_POWEROFF,
	WL_ACTION_REBOOT,
	WL_ACTION_SUSPEND,
	WL_ACTION_HIBERNATE,
	WL_ACTION_LOGOUT,
	WL_ACTION_RELOAD,
	WL_ACTION_LOCK,
	WL_ACTION_SWITCH,
	WL_ACTION_CANCEL
};

struct waylogout_action {
	enum waylogout_action_type type;
	char *label;
	char symbol[8];
	char *command;
	xkb_keysym_t shortcut;
	struct wl_surface *child_surface; // surface made into subsurface
	struct wl_subsurface *subsurface;
	struct waylogout_surface *parent_surface;
	struct pool_buffer indicator_buffers[2];
	uint32_t indicator_width, indicator_height;
	struct wl_list link;
};

struct waylogout_touch {
	struct waylogout_action *action;
	int32_t id;
};

struct waylogout_hover {
	struct waylogout_action *action;
	bool mouse_down;
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
	struct wl_surface *cursor_surface;
	struct wl_cursor_image *cursor_image;
	struct waylogout_args args;
	struct wl_list actions;
	struct waylogout_action *selected_action;
	struct waylogout_hover hover;
	struct waylogout_touch touch;
	wl_fixed_t scroll_amount;
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
	struct zwlr_layer_surface_v1 *layer_surface;
	struct zwlr_screencopy_frame_v1 *screencopy_frame;
	struct pool_buffer buffers[2];
	struct pool_buffer *current_buffer;
	struct waylogout_fade fade;
	int events_pending;
	bool configured;
	bool frame_pending, dirty;
	uint32_t width, height;
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

struct waylogout_frame_common {
	uint32_t arc_radius;
	uint32_t arc_thickness;
	uint32_t line_width;
	uint32_t inner_radius;
	uint32_t outer_radius;
	uint32_t indicator_diameter;
	uint32_t x_offset;
	uint32_t x_center;
	uint32_t y_center;
	uint32_t n_drawn;
	double symbol_font_size;
	double selected_symbol_font_size;
	double label_font_size;
};


void waylogout_handle_key(struct waylogout_state *state,
		xkb_keysym_t keysym, uint32_t codepoint);
void waylogout_handle_mouse_enter(struct waylogout_state *state,
		struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
void waylogout_handle_mouse_leave(struct waylogout_state *state,
		struct wl_surface *surface);
void waylogout_handle_mouse_motion(struct waylogout_state *state,
		wl_fixed_t x, wl_fixed_t y);
void waylogout_handle_mouse_scroll(struct waylogout_state *state,
		wl_fixed_t amount);
void waylogout_handle_mouse_button(struct waylogout_state *state,
		uint32_t button, uint32_t btn_state);
void waylogout_handle_touch_down(struct waylogout_state *state,
		struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y);
void waylogout_handle_touch_up(struct waylogout_state *state, int32_t id);
void waylogout_handle_touch_motion(struct waylogout_state *state,
		int32_t id, wl_fixed_t x, wl_fixed_t y);



void render_frame_background(struct waylogout_surface *surface);
void render_background_fade(struct waylogout_surface *surface, uint32_t time);
void render_background_fade_prepare(struct waylogout_surface *surface, struct pool_buffer *buffer);
void render_frame(struct waylogout_action *action,
		struct waylogout_surface *surface,
		struct waylogout_frame_common fr_common);
void render_frames(struct waylogout_surface *surface);
void damage_surface(struct waylogout_surface *surface);
void damage_state(struct waylogout_state *state);

#endif
