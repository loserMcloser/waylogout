#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <locale.h>
#include <wayland-client.h>
#include "cairo.h"
#include "background-image.h"
#include "waylogout.h"

#include "log.h" // TODO remove

#define M_PI 3.14159265358979323846

static void set_color_for_state(cairo_t *cairo, bool selected,
		struct waylogout_colorset *colorset) {
	cairo_set_source_u32(cairo, selected ? colorset->highlight : colorset->normal);
	// TODO mouse position state
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
	wl_surface_damage(surface->surface, 0, 0, surface->width, surface->height);
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

void render_frame(struct waylogout_surface *surface) {
	struct waylogout_state *state = surface->state;

	int arc_radius = state->args.radius * surface->scale;
	int arc_thickness = state->args.thickness * surface->scale;
	int indicator_diameter = (arc_radius + arc_thickness) * 2;

	// TODO turn indicator_sep into an option
	int indicator_sep = 3;
	int x_offset = indicator_diameter + indicator_sep;

	int buffer_width = surface->indicator_width;
	int buffer_height = surface->indicator_height;
	int new_width = 0;
	int new_height = indicator_diameter;

	int subsurf_xpos;
	int subsurf_ypos;

	// Center the indicators unless overridden by the user
	if (state->args.override_indicator_x_position) {
		subsurf_xpos = state->args.indicator_x_position -
			buffer_width / (2 * surface->scale) + 2 / surface->scale;
	} else {
		subsurf_xpos = surface->width / 2 -
			buffer_width / (2 * surface->scale) + 2 / surface->scale;
	}

	if (state->args.override_indicator_y_position) {
		subsurf_ypos = state->args.indicator_y_position -
			(state->args.radius + state->args.thickness);
	} else {
		subsurf_ypos = surface->height / 2 -
			(state->args.radius + state->args.thickness);
	}

	waylogout_log(LOG_DEBUG, "width/height/pos: %d %d %d %d", surface->width, surface->height, subsurf_xpos, subsurf_ypos);
	waylogout_log(LOG_DEBUG, "indicator width/height: %d %d", buffer_width, buffer_height);

	wl_subsurface_set_position(surface->subsurface, subsurf_xpos, subsurf_ypos);

	surface->current_buffer = get_next_buffer(state->shm,
			surface->indicator_buffers, buffer_width, buffer_height);
	if (surface->current_buffer == NULL) {
		return;
	}

	// Hide subsurface until we want it visible
	wl_surface_attach(surface->child, NULL, 0, 0);
	wl_surface_commit(surface->child);

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

	int n_actions_drawn = 0;
	for (int i = 0; i < N_WAYLOGOUT_ACTIONS; ++i) {

		waylogout_log(LOG_DEBUG, "show for action %d: %d", i, state->actions[i].show);

		if (! state->actions[i].show)
			continue;

		bool selected = state->actions[i].selected;

		int this_width = indicator_diameter;

		double x_pos = (buffer_width / 2.0f) - ((state->n_actions - 1) / 2.0f - n_actions_drawn) * x_offset;

		// Draw circle
		cairo_set_line_width(cairo, arc_thickness);
		cairo_arc(cairo, x_pos, indicator_diameter / 2, arc_radius,
				0, 2 * M_PI);
		set_color_for_state(cairo, selected, &state->args.colors.inside);
		cairo_fill_preserve(cairo);
		set_color_for_state(cairo, selected, &state->args.colors.ring);
		cairo_stroke(cairo);

		// Draw symbol and label
		double font_size;
		if (state->args.font_size > 0) {
			font_size = state->args.font_size;
		} else if (state->args.labels) {
			font_size = arc_radius / 3.0f;
		} else {
			font_size = arc_radius / 1.5f;
		}
		cairo_set_font_size(cairo, font_size);
		set_color_for_state(cairo, selected, &state->args.colors.text);

		cairo_text_extents_t extents;
		cairo_font_extents_t fe;

		double x,y;
		y = 0.0f;

		if (state->args.labels) {
			cairo_select_font_face(cairo, state->args.font,
				CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			char* text = state->actions[i].label;
			cairo_text_extents(cairo, text, &extents);
			cairo_font_extents(cairo, &fe);
			x = x_pos - (extents.width / 2 + extents.x_bearing);
			y = (indicator_diameter / 2.0f) + (fe.height / 2 - fe.descent);
			cairo_move_to(cairo, x, y);
			cairo_show_text(cairo, text);
			cairo_close_path(cairo);
			cairo_new_sub_path(cairo);
			if (this_width < extents.width)
				this_width = extents.width;
		}

		// char symbol[4] = "";  // TODO symbol
		char *symbol = state->actions[i].symbol;
		cairo_select_font_face(
		  cairo,
		  "Font Awesome 5 Free",
		  CAIRO_FONT_SLANT_NORMAL,
		  //   CAIRO_FONT_WEIGHT_NORMAL
		  CAIRO_FONT_WEIGHT_BOLD
		);
		cairo_text_extents(cairo, symbol, &extents);
		cairo_font_extents(cairo, &fe);
		x = x_pos - (extents.width / 2 + extents.x_bearing);
		if (state->args.labels)
			y -= fe.height * 1.25f;
		else
			y = (indicator_diameter / 2.0f) + (fe.height / 2 - fe.descent) - fe.height / 10;
		cairo_move_to(cairo, x, y);
		cairo_show_text(cairo, symbol);
		cairo_close_path(cairo);
		cairo_new_sub_path(cairo);
		if (this_width < extents.width) {
			this_width = extents.width;
		}

		// Draw inner + outer border of the circle
		set_color_for_state(cairo, selected, &state->args.colors.line);
		cairo_set_line_width(cairo, 2.0 * surface->scale);
		cairo_arc(cairo, x_pos, indicator_diameter / 2,
				arc_radius - arc_thickness / 2, 0, 2 * M_PI);
		cairo_stroke(cairo);
		cairo_arc(cairo, x_pos, indicator_diameter / 2,
				arc_radius + arc_thickness / 2, 0, 2 * M_PI);
		cairo_stroke(cairo);

		waylogout_log(LOG_DEBUG, "new width for %d: %d", i, new_width);

		new_width += this_width + indicator_sep;
		++n_actions_drawn;
	}

	new_width -= indicator_sep;

	if (buffer_width != new_width || buffer_height != new_height) {
		destroy_buffer(surface->current_buffer);
		surface->indicator_width = new_width;
		surface->indicator_height = new_height;
		render_frame(surface);
	}

	wl_surface_set_buffer_scale(surface->child, surface->scale);
	wl_surface_attach(surface->child, surface->current_buffer->buffer, 0, 0);
	wl_surface_damage(surface->child, 0, 0, surface->current_buffer->width, surface->current_buffer->height);
	wl_surface_commit(surface->child);

	wl_surface_commit(surface->surface);

}

// void render_frames(struct waylogout_state *state) {
// 	struct waylogout_surface *surface;
// 	wl_list_for_each(surface, &state->surfaces, link) {
// 		render_frame(surface);
// 	}
// }
