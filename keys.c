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

	int i,j;
	bool selection_made;

	switch (keysym) {
	case XKB_KEY_KP_Enter: /* fallthrough */
	case XKB_KEY_Return:
		// TODO activate selected action
		break;
	case XKB_KEY_Escape:
		state->run_display = false;
		break;
	case XKB_KEY_Left:
		selection_made = false;
		i = state->selected_action;
		if (i > -1)
			state->actions[i].selected = false;
		else
			i = 0;
		while (!selection_made) {
			if (i == 0)
				i = N_WAYLOGOUT_ACTIONS;
			--i;
			if (state->actions[i].show) {
				state->actions[i].selected = true;
				state->selected_action = i;
				selection_made = true;
			}
		}
		break;
	case XKB_KEY_Right:
		selection_made = false;
		i = state->selected_action;
		if (i > -1)
			state->actions[i].selected = false;
		while (!selection_made) {
			if (++i == N_WAYLOGOUT_ACTIONS)
				i = 0;
			if (state->actions[i].show) {
				state->actions[i].selected = true;
				state->selected_action = i;
				selection_made = true;
			}
		}
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
	default:
		for (i = 0; i < N_WAYLOGOUT_ACTIONS; ++i)
			if (state->actions[i].show && state->actions[i].shortcut == keysym) {
				break;
			}
		if (i < N_WAYLOGOUT_ACTIONS) {
			state->selected_action = i;
			for (j = 0; j < N_WAYLOGOUT_ACTIONS; ++j)
				state->actions[j].selected = (j == i);
		}
	}
}
