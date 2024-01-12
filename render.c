#include <wayland-client.h>
#include "cairo.h"
#include "background-image.h"
#include "waylogout.h"

#define M_PI 3.14159265358979323846

static void set_color_for_state(cairo_t *cairo, bool selected,
		struct waylogout_colorset *colorset) {
	cairo_set_source_u32(cairo, selected ? colorset->selected : colorset->normal);
}

void render_frame_background(struct waylogout_surface *surface) {
	struct waylogout_state *state = surface->state;

	int buffer_width = surface->width * surface->scale;
	int buffer_height = surface->height * surface->scale;
	if (buffer_width == 0 || buffer_height == 0) {
		return; // not yet configured
	}

	surface->current_buffer = get_next_buffer(state->shm,
			surface->buffers, buffer_width, buffer_height);
	if (surface->current_buffer == NULL) {
		return;
	}

	cairo_t *cairo = surface->current_buffer->cairo;
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

	cairo_save(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_u32(cairo, state->args.colors.background);
	cairo_paint(cairo);
	if (surface->image && state->args.mode != BACKGROUND_MODE_SOLID_COLOR) {
		cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
		render_background_image(cairo, surface->image,
			state->args.mode, buffer_width, buffer_height);
	}
	cairo_restore(cairo);
	cairo_identity_matrix(cairo);

	wl_surface_set_buffer_scale(surface->surface, surface->scale);
	wl_surface_attach(surface->surface, surface->current_buffer->buffer, 0, 0);
	wl_surface_damage_buffer(surface->surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(surface->surface);
}

void render_background_fade(struct waylogout_surface *surface, uint32_t time) {
	struct waylogout_state *state = surface->state;

	int buffer_width = surface->width * surface->scale;
	int buffer_height = surface->height * surface->scale;
	if (buffer_width == 0 || buffer_height == 0) {
		return; // not yet configured
	}

	if (fade_is_complete(&surface->fade)) {
		return;
	}

	surface->current_buffer = get_next_buffer(state->shm,
			surface->buffers, buffer_width, buffer_height);
	if (surface->current_buffer == NULL) {
		return;
	}

	fade_update(&surface->fade, surface->current_buffer, time);

	wl_surface_set_buffer_scale(surface->surface, surface->scale);
	wl_surface_attach(surface->surface, surface->current_buffer->buffer, 0, 0);
	wl_surface_damage(surface->surface, 0, 0, surface->width, surface->height);
	wl_surface_commit(surface->surface);
}

void render_background_fade_prepare(struct waylogout_surface *surface, struct pool_buffer *buffer) {
	if (fade_is_complete(&surface->fade)) {
		return;
	}

	fade_prepare(&surface->fade, buffer);

	wl_surface_set_buffer_scale(surface->surface, surface->scale);
	wl_surface_attach(surface->surface, surface->current_buffer->buffer, 0, 0);
	wl_surface_damage(surface->surface, 0, 0, surface->width, surface->height);
	wl_surface_commit(surface->surface);
}

void render_frame(struct waylogout_action *action,
		struct waylogout_surface *surface,
		struct waylogout_frame_common fr_common) {
	struct waylogout_state *state = surface->state;

	bool selected = (action == state->selected_action);

	int buffer_width = action->indicator_width;
	int buffer_height = action->indicator_height;
	int new_width = fr_common.indicator_diameter;
	int new_height = fr_common.indicator_diameter;

	double indicator_xcenter = fr_common.x_center - ((wl_list_length(&state->actions) - 1) / 2.0f - fr_common.n_drawn) * fr_common.x_offset;

	double dbl_subsurf_xcenter = indicator_xcenter -
			buffer_width / (2.0f * surface->scale) + 2 / (1.0f * surface->scale);
	int subsurf_xcenter = dbl_subsurf_xcenter;

	int subsurf_ycenter = fr_common.y_center -
			(state->args.radius + state->args.thickness);

	if (selected && state->hover.mouse_down &&
			state->hover.action == state->selected_action) {
		subsurf_xcenter += 2;
		subsurf_ycenter += 2;
	}

	wl_subsurface_set_position(action->subsurface, subsurf_xcenter, subsurf_ycenter);

	// TODO should each action get its own current_buffer pointer?
	surface->current_buffer = get_next_buffer(state->shm,
			action->indicator_buffers, buffer_width, buffer_height);
	if (surface->current_buffer == NULL) {
		return;
	}

	// Hide subsurface until we want it visible
	wl_surface_attach(action->child_surface, NULL, 0, 0);
	wl_surface_commit(action->child_surface);

	cairo_t *cairo = surface->current_buffer->cairo;
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
	cairo_font_options_set_subpixel_order(fo, to_cairo_subpixel_order(surface->subpixel));
	cairo_set_font_options(cairo, fo);
	cairo_font_options_destroy(fo);
	cairo_identity_matrix(cairo);

	// Clear
	cairo_save(cairo);
	cairo_set_source_rgba(cairo, 0, 0, 0, 0);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cairo);
	cairo_restore(cairo);

	double relative_xcenter = buffer_width / 2.0f;
	double relative_ycenter = fr_common.indicator_diameter / 2.0f;

	// Splitting up inner circle fill from ring stroke to avoid the two-tone ring affect.
	// https://github.com/swaywm/swaylock/issues/113

	// Draw inner circle
	cairo_set_line_width(cairo, 0);
	cairo_arc(cairo, relative_xcenter, relative_ycenter,
			fr_common.arc_radius - fr_common.arc_thickness / 2, 0, 2 * M_PI);
	set_color_for_state(cairo, selected, &state->args.colors.inside);
	cairo_fill_preserve(cairo);
	cairo_stroke(cairo);

	// Draw ring
	cairo_set_line_width(cairo, fr_common.arc_thickness);
	cairo_arc(cairo, relative_xcenter, relative_ycenter,
			fr_common.arc_radius, 0, 2 * M_PI);
	set_color_for_state(cairo, selected, &state->args.colors.ring);
	cairo_stroke(cairo);

	// Draw symbol and label
	set_color_for_state(cairo, selected, &state->args.colors.text);

	cairo_text_extents_t extents;
	cairo_font_extents_t fe;

	double x,y;
	y = 0.0f;

	bool show_label = state->args.labels || (state->args.selection_label && selected);

	if (show_label) {
		cairo_set_font_size(cairo, fr_common.label_font_size);
		cairo_select_font_face(cairo, state->args.font,
			CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		char* text = action->label;
		cairo_text_extents(cairo, text, &extents);
		cairo_font_extents(cairo, &fe);
		x = relative_xcenter - (extents.width / 2 + extents.x_bearing);
		y = relative_ycenter + (fe.height / 2 - fe.descent);
		cairo_move_to(cairo, x, y);
		cairo_show_text(cairo, text);
		cairo_close_path(cairo);
		cairo_new_sub_path(cairo);
		if (new_width < extents.width)
			new_width = extents.width;
	}

	char *symbol = action->symbol;
	cairo_set_font_size(cairo, selected ?
		fr_common.selected_symbol_font_size : fr_common.symbol_font_size);
	cairo_select_font_face(
		cairo,
		state->args.fa_font,
		CAIRO_FONT_SLANT_NORMAL,
		//   CAIRO_FONT_WEIGHT_NORMAL
		CAIRO_FONT_WEIGHT_BOLD
	);
	cairo_text_extents(cairo, symbol, &extents);
	cairo_font_extents(cairo, &fe);
	x = relative_xcenter - (extents.width / 2 + extents.x_bearing);
	y = relative_ycenter;
	if (show_label)
		y = 3 * y / 5;
	y += (fe.height / 2 - fe.descent);
	if (show_label)
		y += fe.height / 5;
	cairo_move_to(cairo, x, y);
	cairo_show_text(cairo, symbol);
	cairo_close_path(cairo);
	cairo_new_sub_path(cairo);
	if (new_width < extents.width) {
		new_width = extents.width;
	}

	// Draw inner + outer border of the circle
	set_color_for_state(cairo, selected, &state->args.colors.line);
	cairo_set_line_width(cairo, fr_common.line_width);
	cairo_arc(cairo, relative_xcenter, relative_ycenter,
			fr_common.inner_radius, 0, 2 * M_PI);
	cairo_stroke(cairo);
	cairo_arc(cairo, relative_xcenter, relative_ycenter,
			fr_common.outer_radius, 0, 2 * M_PI);
	cairo_stroke(cairo);

	// Ensure buffer size is multiple of buffer scale - required by protocol
	new_height += surface->scale - (new_height % surface->scale);
	new_width += surface->scale - (new_width % surface->scale);

	if (buffer_width != new_width || buffer_height != new_height) {
		destroy_buffer(surface->current_buffer);
		action->indicator_width = new_width;
		action->indicator_height = new_height;
		render_frame(action, surface, fr_common);
		return;
	}

	wl_surface_set_buffer_scale(action->child_surface, surface->scale);
	wl_surface_attach(action->child_surface, surface->current_buffer->buffer, 0, 0);
	wl_surface_damage_buffer(action->child_surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(action->child_surface);

	wl_surface_commit(surface->surface);

}

void render_frames(struct waylogout_surface *surface) {
	struct waylogout_state *state = surface->state;

	struct waylogout_frame_common fr_common;
	fr_common.arc_radius = state->args.radius * surface->scale;
	fr_common.arc_thickness = state->args.thickness * surface->scale;
	fr_common.line_width = 2.0 * surface->scale;
	fr_common.inner_radius = fr_common.arc_radius - fr_common.arc_thickness / 2;
	fr_common.outer_radius = fr_common.arc_radius + fr_common.arc_thickness / 2;

	fr_common.indicator_diameter = fr_common.arc_radius * 2
			+ fr_common.arc_thickness + fr_common.line_width;

	int n_actions = wl_list_length(&state->actions);
	int indicator_sep = (state->args.indicator_sep > 0)
	  ? (int) state->args.indicator_sep
	  : (int) (surface->width * surface->scale - n_actions * fr_common.indicator_diameter)
	    / (n_actions + 1)
	;
	if (indicator_sep < 0)
		indicator_sep = fr_common.arc_thickness;

	// TODO should this be divided by surface->scale ?
	fr_common.x_offset = fr_common.indicator_diameter + indicator_sep;

	fr_common.x_center = (state->args.override_indicator_x_position)
			? state->args.indicator_x_position
			: surface->width / 2;

	fr_common.y_center = (state->args.override_indicator_y_position)
			? state->args.indicator_y_position
			: surface->height / 2;

	if (state->args.symbol_font_size > 0)
		fr_common.symbol_font_size = state->args.symbol_font_size;
	else if (state->args.labels)
		if (state->args.label_font_size > 0)
			fr_common.symbol_font_size = state->args.label_font_size;
		else
			fr_common.symbol_font_size = fr_common.arc_radius / 3.0f;
	else
		fr_common.symbol_font_size = fr_common.arc_radius / 1.5f;

	if (state->args.label_font_size > 0)
		fr_common.label_font_size = state->args.label_font_size;
	else
		fr_common.label_font_size = fr_common.arc_radius / 3.0f;

	if (state->args.selection_label && !state->args.labels)
		fr_common.selected_symbol_font_size = fr_common.label_font_size;
	else
		fr_common.selected_symbol_font_size = fr_common.symbol_font_size;

	struct waylogout_action *action_iter;
	fr_common.n_drawn = 0;
	wl_list_for_each(action_iter, &state->actions, link) {
		render_frame(action_iter, surface, fr_common);
		++fr_common.n_drawn;
	}

}
