#define _POSIX_C_SOURCE 200809L
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include <string.h>
#include "action.h"
#include "helpers.h"
#include "input.h"
#include "log.h"
#include "loop.h"
#include "seat.h"
#include "waylogout.h"

bool compare_shortcuts(struct waylogout_keycombo *combo1, struct waylogout_keycombo *combo2) {
	if (combo1->key == XKB_KEY_NoSymbol)
		return false;
	return (combo1->key == combo2->key) &&
			(combo1->control == combo2->control) &&
			(combo1->alt == combo2->alt);
}

void parse_shortcut(char *keycombo, struct waylogout_keycombo *shortcut) {
	*shortcut = (struct waylogout_keycombo) {
		.key = XKB_KEY_NoSymbol,
		.control = false,
		.alt = false,
		.deleted = false
	};
	if (!keycombo)
		return;
	char *separator;
	char *chunk;
	char *keycombo_orig = strdup_noquotes(keycombo);
	char *keycombo_working = strdup_noquotes(keycombo);
	char *keycombo_working_orig = keycombo_working;
	while ((separator = strchr(keycombo_working, '+'))) {
		*separator = '\0';
		chunk = strdup(keycombo_working);
		if (strncmp(chunk, "Ctrl", 4) == 0)
			shortcut->control = true;
		else if (strncmp(chunk, "Alt", 3) == 0)
			shortcut->alt = true;
		else {
			waylogout_log(LOG_ERROR,
					"Could not parse shortcut config string \"%s\" --- unknown modifier \"%s\"",
					keycombo_orig, chunk);
			free(keycombo_orig);
			free(keycombo_working_orig);
			free(chunk);
			return;
		}
		keycombo_working = separator + 1;
		free(chunk);
	}
	shortcut->key = xkb_keysym_from_name(keycombo_working, 0);
	if (shortcut->key == XKB_KEY_NoSymbol) {
		waylogout_log(LOG_ERROR,
				"Could not parse shortcut config string \"%s\" --- unknown key \"%s\"",
				keycombo_orig, keycombo_working);
	}
	free(keycombo_orig);
	free(keycombo_working_orig);
}

void mouse_enter_motion_selection(struct waylogout_state *state, int x, int y) {
	struct waylogout_action_surface *hovered_surface = state->hovered_surface;
	if (!hovered_surface)
		return;
	struct waylogout_action *action = hovered_surface->action;
	int x_diff = (x - hovered_surface->indicator_width / 2);
	int y_diff = (y - hovered_surface->indicator_width / 2);
	int radius = (state->args.radius + state->args.thickness / 2) * hovered_surface->parent_surface->scale;
	if (x_diff * x_diff + y_diff * y_diff < radius * radius) {
		state->selected_action = action;
		damage_state(state);
	} else {
		if (state->selected_action == action) {
			state->selected_action = NULL;
			damage_state(state);
		}
	}
}

void waylogout_handle_mouse_enter(struct waylogout_state *state,
		struct wl_surface *wl_surface, wl_fixed_t x, wl_fixed_t y) {
	struct waylogout_surface *surface;
	wl_list_for_each(surface, &state->surfaces, link) {
		struct waylogout_action_surface *action_surface;
		wl_array_for_each(action_surface, &surface->children) {
			if (wl_surface == action_surface->surface) {
				state->hovered_surface = action_surface;
				mouse_enter_motion_selection(state, wl_fixed_to_int(x), wl_fixed_to_int(y));
				return;
			}
		}
	}
}

void waylogout_handle_mouse_leave(struct waylogout_state *state,
		struct wl_surface *wl_surface) {
	struct waylogout_surface *surface;
	wl_list_for_each(surface, &state->surfaces, link) {
		struct waylogout_action_surface *action_surface;
		wl_array_for_each(action_surface, &surface->children) {
			if (wl_surface == action_surface->surface) {
				if (action_surface == state->hovered_surface) {
					state->hovered_surface = NULL;
					state->selected_action_depressed = false;
				}
				if (action_surface->action == state->selected_action) {
					state->selected_action = NULL;
					damage_state(state);
				}
			}
			return;
		}
	}
}

void waylogout_handle_mouse_motion(struct waylogout_state *state,
		wl_fixed_t x, wl_fixed_t y) {
	if (state->hovered_surface)
		mouse_enter_motion_selection(state, wl_fixed_to_int(x), wl_fixed_to_int(y));
}

void waylogout_handle_mouse_scroll(struct waylogout_state *state,
		wl_fixed_t amount) {
	state->scroll_amount += amount;
	if (state->scroll_amount > (int) state->args.scroll_sensitivity) {
		select_next_action(state);
		state->scroll_amount = 0;
	} else if (state->scroll_amount < -1 * ((int) state->args.scroll_sensitivity)) {
		select_prev_action(state);
		state->scroll_amount = 0;
	}
}

void waylogout_handle_mouse_button(struct waylogout_state *state,
			uint32_t button, uint32_t btn_state) {
	if (button == BTN_LEFT) {
		if (state->hovered_surface && state->hovered_surface->action == state->selected_action) {
			if (btn_state) {  // pressed
				state->selected_action_depressed = true;
				damage_state(state);
			} else {
				state->run_action_now = true;
			}
		}
	} else if (button == BTN_MIDDLE && state->selected_action) {
		state->selected_action_depressed = true;
		damage_state(state);
		state->run_action_now = true;
	}
}

void waylogout_handle_touch_down(struct waylogout_state *state,
		struct wl_surface *wl_surface, int32_t id, wl_fixed_t x, wl_fixed_t y) {
	struct waylogout_surface *surface;
	wl_list_for_each(surface, &state->surfaces, link) {
		struct waylogout_action_surface *action_surface;
		wl_array_for_each(action_surface, &surface->children) {
			if (wl_surface == action_surface->surface) {
				state->hovered_surface = action_surface;
				state->touch = (struct waylogout_touch) {
					.action = action_surface->action,
					.id = id
				};
				mouse_enter_motion_selection(state, wl_fixed_to_int(x), wl_fixed_to_int(y));
				return;
			}
		}
	}
}

void waylogout_handle_touch_up(struct waylogout_state *state, int32_t id) {
	if (id != state->touch.id)
		return;
	if (state->selected_action == state->touch.action)
		state->run_action_now = true;
}

void waylogout_handle_touch_motion(struct waylogout_state *state,
		int32_t id, wl_fixed_t x, wl_fixed_t y) {
	if (id != state->touch.id)
		return;
	mouse_enter_motion_selection(state, wl_fixed_to_int(x), wl_fixed_to_int(y));
}

void waylogout_handle_key(struct waylogout_state *state,
		xkb_keysym_t keysym, uint32_t codepoint) {

	struct waylogout_action *action;

	if (! (state->xkb.control || state->xkb.alt)) {

		switch (keysym) {
		case XKB_KEY_KP_Enter: /* fallthrough */
		case XKB_KEY_Return:
			if (state->selected_action) {
				state->selected_action_depressed = true;
				damage_state(state);
				state->run_action_now = true;
			}
			return;
		case XKB_KEY_Escape:
			if (state->args.hide_cancel)
				state->run_display = false;
			else {
				state->selected_action = find_action(&state->actions, WL_ACTION_CANCEL);
				state->selected_action_depressed = true;
				damage_state(state);
				state->run_action_now = true;
			}
			return;
		case XKB_KEY_Down: /* fallthrough */
		case XKB_KEY_KP_Down:
			if (state->args.reverse_arrows)
				select_next_action(state);
			else
				select_prev_action(state);
			return;
		case XKB_KEY_Left: /* fallthrough */
		case XKB_KEY_KP_Left: /* fallthrough */
		case XKB_KEY_ISO_Left_Tab:
			select_prev_action(state);
			return;
		case XKB_KEY_Up: /* fallthrough */
		case XKB_KEY_KP_Up:
			if (state->args.reverse_arrows)
				select_prev_action(state);
			else
				select_next_action(state);
			return;
		case XKB_KEY_Right: /* fallthrough */
		case XKB_KEY_KP_Right: /* fallthrough */
		case XKB_KEY_Tab:
			select_next_action(state);
			return;
		case XKB_KEY_Home: /* fallthrough */
		case XKB_KEY_KP_Home:
			select_first_action(state);
			return;
		case XKB_KEY_End: /* fallthrough */
		case XKB_KEY_KP_End:
			select_last_action(state);
			return;
		case XKB_KEY_F1:
			codepoint = 1;
			/* fallthrough */
		case XKB_KEY_F2:
			if (codepoint == 0)
				codepoint = 2;
			/* fallthrough */
		case XKB_KEY_F3:
			if (codepoint == 0)
				codepoint = 3;
			/* fallthrough */
		case XKB_KEY_F4:
			if (codepoint == 0)
				codepoint = 4;
			/* fallthrough */
		case XKB_KEY_F5:
			if (codepoint == 0)
				codepoint = 5;
			/* fallthrough */
		case XKB_KEY_F6:
			if (codepoint == 0)
				codepoint = 6;
			/* fallthrough */
		case XKB_KEY_F7:
			if (codepoint == 0)
				codepoint = 7;
			/* fallthrough */
		case XKB_KEY_F8:
			if (codepoint == 0)
				codepoint = 8;
			/* fallthrough */
		case XKB_KEY_F9:
			if (codepoint == 0)
				codepoint = 9;
			/* fallthrough */
		case XKB_KEY_F10:
			if (codepoint == 0)
				codepoint = 10;
			/* fallthrough */
		case XKB_KEY_F11:
			if (codepoint == 0)
				codepoint = 11;
			/* fallthrough */
		case XKB_KEY_F12:
			if (codepoint == 0)
				codepoint = 12;
			/* fallthrough */
		case XKB_KEY_0:
		case XKB_KEY_1:
		case XKB_KEY_2:
		case XKB_KEY_3:
		case XKB_KEY_4:
		case XKB_KEY_5:
		case XKB_KEY_6:
		case XKB_KEY_7:
		case XKB_KEY_8:
		case XKB_KEY_9:
			if (codepoint == 48)
				codepoint = 58;
			if (codepoint > 12)
				codepoint -= 48;
			codepoint = codepoint % wl_list_length(&state->actions);
			codepoint = (codepoint == 0) ? (uint32_t) wl_list_length(&state->actions) : codepoint;
			struct wl_list *list_iter = &state->actions;
			for (uint32_t count = 0; count < codepoint; ++count)
				list_iter = list_iter->next;
			state->selected_action = wl_container_of(list_iter, action, link);
			damage_state(state);
			return;
		default:
			break;
		}

	}

	// if we reach here, keypress has not been handled
	char keyname[64];
	xkb_keysym_get_name(keysym, keyname, 64);
	waylogout_log(LOG_DEBUG, "Keypress: %s  Ctrl: %d  Alt: %d", keyname,
			state->xkb.control, state->xkb.alt);
	struct waylogout_keycombo keycombo = (struct waylogout_keycombo) {
		.control = state->xkb.control,
		.alt = state->xkb.alt,
		.deleted = false,
		.key = keysym
	};
	wl_list_for_each(action, &state->actions, link) {
		if (compare_shortcuts(&action->shortcut, &keycombo)) {
			state->selected_action = action;
			if (state->args.instant_run)
				state->selected_action_depressed = true;
			damage_state(state);
			state->run_action_now = state->args.instant_run;
			break;
		}
	}

}
