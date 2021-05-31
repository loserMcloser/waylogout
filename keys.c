#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include "comm.h"
#include "log.h"
#include "loop.h"
#include "seat.h"
#include "swaylock.h"
#include "unicode.h"

// TODO handle mouse clicks
void swaylock_handle_mouse(struct swaylock_state *state) {
	// if (state->auth_state == AUTH_STATE_GRACE && !state->args.password_grace_no_mouse) {
	// 	state->run_display = false;
	// }
}

// TODO handle touch "clicks"
void swaylock_handle_touch(struct swaylock_state *state) {
	// if (state->auth_state == AUTH_STATE_GRACE && !state->args.password_grace_no_touch) {
	// 	state->run_display = false;
	// } else if (state->auth_state != AUTH_STATE_VALIDATING && state->args.password_submit_on_touch) {
	// 	submit_password(state);
	// }
}

void swaylock_handle_key(struct swaylock_state *state,
		xkb_keysym_t keysym, uint32_t codepoint) {

	// TODO handle navigation by arrow keys

	switch (keysym) {
	case XKB_KEY_KP_Enter: /* fallthrough */
	case XKB_KEY_Return:
		// TODO activate highlighted action, if navigating by arrow keys
		break;
	case XKB_KEY_Escape:
		state->run_display = false;
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
	// case XKB_KEY_m:
	// case XKB_KEY_d:
	// case XKB_KEY_j:
	// case XKB_KEY_c:
	// case XKB_KEY_u:
	default:
		// if (codepoint) { }
			// codepoint ?
			// see original: https://github.com/swaywm/swaylock/blob/master/password.c
		break;
	}
}
