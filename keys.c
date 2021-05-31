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

// TODO handle mouse clicks
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

	// TODO handle navigation by arrow keys

	struct waylogout_action *action_iter;

	switch (keysym) {
	case XKB_KEY_KP_Enter: /* fallthrough */
	case XKB_KEY_Return:
		// TODO activate selected action
		break;
	case XKB_KEY_Escape:
		state->run_display = false;
		break;
	case XKB_KEY_Left:
		if (state->selected_action)
			state->selected_action = state->selected_action->prev;
		else
			state->selected_action = state->last_action;
		damage_state(state);
		break;
	case XKB_KEY_Right:
		if (state->selected_action)
			state->selected_action = state->selected_action->next;
		else
			state->selected_action = state->first_action;
		damage_state(state);
		break;
	// case XKB_KEY_Caps_Lock:
	// case XKB_KEY_Shift_L:
	// case XKB_KEY_Shift_R:
	// case XKB_KEY_Control_L:
	// case XKB_KEY_Control_R:
	// case XKB_KEY_Meta_L:
	// case XKB_KEY_Meta_R:
	// case XKB_KEY_Alt_L:
	// case XKB_KEY_Alt_R:
	// case XKB_KEY_Super_L:
	// case XKB_KEY_Super_R:
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
		action_iter = state->last_action;
		for (uint32_t count = 0; count < codepoint; ++count)
			action_iter = action_iter->next;
		state->selected_action = action_iter;
		damage_state(state);
		break;
	default:
		action_iter = state->first_action;
		while (true) {
			if (action_iter->shortcut == keysym) {
				state->selected_action = action_iter;
				damage_state(state);
				break;
			}
			if (action_iter == state->last_action)
				break;
			action_iter = action_iter->next;
		}
	}
}
