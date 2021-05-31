#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include "log.h"
#include "loop.h"
#include "seat.h"
#include "waylogout.h"
#include "unicode.h"
#include "log.h"

void select_next_action(struct waylogout_state *state) {
	struct wl_list *selection;
	if (state->selected_action) {
		selection = state->selected_action->link.next;
		if (selection == &state->actions)
			selection = state->actions.next;
	} else
		selection = state->actions.next;
	state->selected_action =
			wl_container_of(selection, state->selected_action, link);
	damage_state(state);
}

void select_prev_action(struct waylogout_state *state) {
	struct wl_list *selection;
	if (state->selected_action) {
		selection = state->selected_action->link.prev;
		if (selection == &state->actions)
			selection = state->actions.prev;
	} else
		selection = state->actions.prev;
	state->selected_action =
			wl_container_of(selection, state->selected_action, link);
	damage_state(state);
}

void mouse_enter_motion_selection(struct waylogout_state *state,
		struct waylogout_action *action, int x, int y) {
	int x_diff = (x - action->indicator_width / 2);
	int y_diff = (y - action->indicator_width / 2);
	int radius = (state->args.radius + state->args.thickness / 2) * action->parent_surface->scale;
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
		struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
	struct waylogout_action *action_iter;
	wl_list_for_each(action_iter, &state->actions, link)
		if (surface == action_iter->child_surface) {
			state->hovered_action = action_iter;
			mouse_enter_motion_selection(state, action_iter,
					wl_fixed_to_int(x), wl_fixed_to_int(y));
			break;
		}
}

void waylogout_handle_mouse_leave(struct waylogout_state *state,
		struct wl_surface *surface) {
	struct waylogout_action *action_iter;
	wl_list_for_each(action_iter, &state->actions, link)
		if (surface == action_iter->child_surface) {
			if (action_iter == state->hovered_action)
				state->hovered_action = NULL;
			if (action_iter == state->selected_action)
				state->selected_action = NULL;
			break;
		}
}

void waylogout_handle_mouse_motion(struct waylogout_state *state,
		wl_fixed_t x, wl_fixed_t y) {
	struct waylogout_action *action_iter;
	wl_list_for_each(action_iter, &state->actions, link)
		if (action_iter == state->hovered_action) {
			mouse_enter_motion_selection(state, action_iter,
					wl_fixed_to_int(x), wl_fixed_to_int(y));
			break;
		}
}

void waylogout_handle_mouse_scroll(struct waylogout_state *state,
		wl_fixed_t amount) {
	// TODO add a "mouse scroll sensitivity" setting
	state->scroll_amount += amount;
	if (state->scroll_amount > (int) state->args.scroll_sensitivity) {
		select_next_action(state);
		state->scroll_amount = 0;
	} else if (state->scroll_amount < -1 * ((int) state->args.scroll_sensitivity)) {
		select_prev_action(state);
		state->scroll_amount = 0;
	}
}

void waylogout_handle_mouse(struct waylogout_state *state) {
	// if (state->auth_state == AUTH_STATE_GRACE && !state->args.password_grace_no_mouse) {
	// 	state->run_display = false;
	// }
}

// TODO handle touch "clicks"
void waylogout_handle_touch(struct waylogout_state *state) {
	// if (state->auth_state == AUTH_STATE_GRACE && !state->args.password_grace_no_touch) {
	// 	state->run_display = false;
	// } else if (state->auth_state != AUTH_STATE_VALIDATING && state->args.password_submit_on_touch) {
	// 	submit_password(state);
	// }
}

void waylogout_handle_key(struct waylogout_state *state,
		xkb_keysym_t keysym, uint32_t codepoint) {

	struct waylogout_action *action_iter;

	switch (keysym) {
	case XKB_KEY_KP_Enter: /* fallthrough */
	case XKB_KEY_Return:
		// TODO activate selected action
		break;
	case XKB_KEY_Escape:
		state->run_display = false;
		break;
	case XKB_KEY_Left: /* fallthrough */
	case XKB_KEY_Down:
		select_prev_action(state);
		break;
	case XKB_KEY_Right: /* fallthrough */
	case XKB_KEY_Up:
		select_next_action(state);
		break;
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
		state->selected_action = wl_container_of(list_iter, action_iter, link);
		damage_state(state);
		break;
	default:
		wl_list_for_each(action_iter, &state->actions, link)
			if (action_iter->shortcut == keysym) {
				state->selected_action = action_iter;
				damage_state(state);
				break;
			}
	}
}
