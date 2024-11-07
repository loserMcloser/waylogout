#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include "action.h"
#include "helpers.h"
#include "log.h"
#include "waylogout.h"

void select_first_action(struct waylogout_state *state) {
	state->selected_action =
			wl_container_of(state->actions.next, state->selected_action, link);
	damage_state(state);
}

void select_last_action(struct waylogout_state *state) {
	state->selected_action =
			wl_container_of(state->actions.prev, state->selected_action, link);
	damage_state(state);
}

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

void add_action(struct waylogout_state *state,
		enum waylogout_action_type type, const char *label, char *symbol,
		char *command, char* keycombo, xkb_keysym_t key) {
	// pass keycombo == NULL to just set up the shortcut as key with no modifiers

	if ((type == WL_ACTION_CANCEL) && (state->args.hide_cancel)) {
		waylogout_log(LOG_ERROR,
				"Config option(s) for \"cancel\" action have been specified, but so has \"hide-cancel\" option --- honoring \"hide-cancel\".");
		return;
	}

	if ((type != WL_ACTION_CUSTOM) && find_action(&state->actions, type))
		return;

	struct waylogout_action *new_action = malloc(sizeof(struct waylogout_action));

	new_action->type = type;
	new_action->label = strdup_noquotes(label); // safe to pass NULL to strdup_noquotes
	if (symbol)
		strncpy(new_action->symbol, symbol, 4);
	else
		new_action->symbol[0] = '\0';
	new_action->command = strdup_noquotes(command); // safe to pass NULL to strdup_noquotes
	if (keycombo) {
		parse_shortcut(keycombo, &new_action->shortcut);
	} else {
		new_action->shortcut = (struct waylogout_keycombo) {
			.control = false,
			.alt = false,
			.deleted = false,
			.key = key
		};
	}
	new_action->rendered_depressed = false;

	for (size_t i = 0; i < 2; ++i)
		new_action->indicator_buffers[i] = (struct pool_buffer){
			.buffer = NULL,
			.surface = NULL,
			.cairo = NULL,
			.width = 0,
			.height = 0,
			.data = NULL,
			.size = 0,
			.busy = false
		};

	// insert new action at end of list
	wl_list_insert(state->actions.prev, &new_action->link);

}

void add_action_label(struct waylogout_state *state,
		enum waylogout_action_type type, char *label) {
	struct waylogout_action *action = find_action(&state->actions, type);
	if (action && (!action->label)) {
		action->label = strdup_noquotes(label);
		return;
	}
	if ((!action) || (type == WL_ACTION_CUSTOM))
		add_action(state, type, label, NULL, NULL, NULL, XKB_KEY_NoSymbol);
}

void add_action_shortcut(struct waylogout_state *state,
		enum waylogout_action_type type, char *keycombo) {
	struct waylogout_action *action = find_action(&state->actions, type);
	if (action && (action->shortcut.key == XKB_KEY_NoSymbol)) {
		parse_shortcut(keycombo, &action->shortcut);
		return;
	}
	if ((!action) || (type == WL_ACTION_CUSTOM))
		add_action(state, type, NULL, NULL, NULL, keycombo, XKB_KEY_NoSymbol);
}

void add_action_symbol(struct waylogout_state *state,
		enum waylogout_action_type type, char *symbol) {
	struct waylogout_action *action = find_action(&state->actions, type);
	if (action && (action->symbol[0] == '\0')) {
		strncpy(action->symbol, symbol, 4);
		return;
	}
	if ((!action) || (type == WL_ACTION_CUSTOM))
		add_action(state, type, NULL, symbol, NULL, NULL, XKB_KEY_NoSymbol);
}

void add_action_command(struct waylogout_state *state,
		enum waylogout_action_type type, char *command) {
	struct waylogout_action *action = find_action(&state->actions, type);
	if (action && (!action->command)) {
		action->command = strdup_noquotes(command);
		return;
	}
	if ((!action) || (type == WL_ACTION_CUSTOM))
		add_action(state, type, NULL, NULL, command, NULL, XKB_KEY_NoSymbol);
}

void set_default_action(struct waylogout_state *state) {
	enum waylogout_action_type default_type;
	if (lenient_strcmp(state->args.default_action, "poweroff") == 0)
		default_type = WL_ACTION_POWEROFF;
	else if (lenient_strcmp(state->args.default_action, "reboot") == 0)
		default_type = WL_ACTION_REBOOT;
	else if (lenient_strcmp(state->args.default_action, "suspend") == 0)
		default_type = WL_ACTION_SUSPEND;
	else if (lenient_strcmp(state->args.default_action, "hibernate") == 0)
		default_type = WL_ACTION_HIBERNATE;
	else if (lenient_strcmp(state->args.default_action, "logout") == 0)
		default_type = WL_ACTION_LOGOUT;
	else if (lenient_strcmp(state->args.default_action, "reload") == 0)
		default_type = WL_ACTION_RELOAD;
	else if (lenient_strcmp(state->args.default_action, "lock") == 0)
		default_type = WL_ACTION_LOCK;
	else if (lenient_strcmp(state->args.default_action, "switch-user") == 0)
		default_type = WL_ACTION_SWITCH;
	else
		default_type = WL_ACTION_NO_ACTION;

	if (default_type == WL_ACTION_NO_ACTION) {
		waylogout_log(LOG_DEBUG, "No default action configured");
		return;
	}

	state->selected_action = find_action(&state->actions, default_type);
	if (state->selected_action) {
		waylogout_log(LOG_INFO, "Set default action to %s", state->args.default_action);
	} else {
		waylogout_log(LOG_ERROR,
				"Requested default action is %s, but that action has not been configured",
				state->args.default_action);
	}
}

void run_action(struct waylogout_state *state, struct waylogout_action *action) {
	if (!action)
		return;
	waylogout_log(LOG_DEBUG, "Running %s action", action->label);
	if (action->type == WL_ACTION_CANCEL) {
		state->run_display = false;
		return;
	}
	waylogout_log(LOG_DEBUG, "%s", action->command);
	char *const cmd[] = { "sh", "-c", action->command, NULL, };
	execvp(cmd[0], cmd);
}

void setup_rows(struct waylogout_state *state) {
	if (state->args.rows > state->n_actions) {
		waylogout_log(LOG_ERROR, "Requested number of rows is greater than number of configured actions.");
		state->args.rows = state->n_actions;
	}
	int per_row = state->n_actions / state->args.rows;
	state->longest_row = per_row;
	uint8_t leftover = state->n_actions % state->args.rows;
	if (leftover > 0)
		++state->longest_row;
	uint8_t shorter = state->args.rows - leftover;
	uint8_t row_tip = (shorter == 1) ? 0 : (shorter / 2 - 1);
	uint8_t row_index;
	for (row_index = 0; row_index < state->args.rows; ++row_index) {
		state->rows[row_index] = per_row;
		if ((row_index > row_tip) && (leftover > 0)) {
			++state->rows[row_index];
			--leftover;
		}
	}
	row_index = 0;
	int n_actions_this_row = 0;
	struct waylogout_action *action;
	wl_list_for_each(action, &state->actions, link) {
		action->row = row_index;
		++n_actions_this_row;
		if (n_actions_this_row == state->rows[row_index]) {
			++row_index;
			n_actions_this_row = 0;
		}
	}
}

struct waylogout_action *find_action(struct wl_list *actions,
		enum waylogout_action_type type){
	// in the case of WL_ACTION_CUSTOM, finds last occurrence
	struct waylogout_action *action;
	struct waylogout_action *ret_action = NULL;
	wl_list_for_each(action, actions, link) {
		if (action->type == type) {
			ret_action = action;
			if (type != WL_ACTION_CUSTOM)
				break;
		}
	}
	return ret_action;
}

int finish_actions_setup(struct waylogout_state *state) {

	struct waylogout_action *action;
	action = find_action(&state->actions, WL_ACTION_CANCEL);
	bool cancel_found = (action != NULL);
	if (cancel_found && state->args.hide_cancel) {
		waylogout_log(LOG_ERROR, "Label or symbol for cancel action configured, "
				"but hide-cancel option also specified.");
		return EXIT_FAILURE;
	}

	const char *default_labels[WL_ACTION_END] = {
		'\0',
		"power off",
		"reboot",
		"sleep",
		"hibernate",
		"logout",
		"reload wm",
		"lock",
		"switch user",
		"cancel",
		"custom"
	};

	char default_symbols[WL_ACTION_END][8];
	default_symbols[WL_ACTION_NO_ACTION][0] = '\0';
	strncpy(default_symbols[WL_ACTION_POWEROFF], "", 4);
	strncpy(default_symbols[WL_ACTION_REBOOT], "", 4);
	strncpy(default_symbols[WL_ACTION_SUSPEND], "", 4);
	strncpy(default_symbols[WL_ACTION_HIBERNATE], "", 4);
	strncpy(default_symbols[WL_ACTION_LOGOUT], "", 4);
	strncpy(default_symbols[WL_ACTION_RELOAD], "", 4);
	strncpy(default_symbols[WL_ACTION_LOCK], "", 4);
	strncpy(default_symbols[WL_ACTION_SWITCH], "", 4);
	strncpy(default_symbols[WL_ACTION_CANCEL], "", 4);
	strcpy(default_symbols[WL_ACTION_CUSTOM], "?");

	const xkb_keysym_t default_shortcut_keys[WL_ACTION_END] = {
		XKB_KEY_NoSymbol,  // no action
		XKB_KEY_p,         // power off
		XKB_KEY_r,         // reboot
		XKB_KEY_s,         // sleep
		XKB_KEY_h,         // hibernate
		XKB_KEY_x,         // logout
		XKB_KEY_c,         // reload
		XKB_KEY_k,         // lock
		XKB_KEY_u,         // switch user
		XKB_KEY_Escape,    // cancel
		XKB_KEY_NoSymbol   // custom
	};

#define N_RESERVED_KEYS 39
	const xkb_keysym_t reserved_keys[N_RESERVED_KEYS] = {
		XKB_KEY_KP_Enter, XKB_KEY_Return, XKB_KEY_Escape, // 3
		XKB_KEY_Down, XKB_KEY_KP_Down, XKB_KEY_Left, XKB_KEY_KP_Left, // 4
		XKB_KEY_ISO_Left_Tab, XKB_KEY_Up, XKB_KEY_KP_Up, XKB_KEY_Right, // 4
		XKB_KEY_KP_Right, XKB_KEY_Tab, XKB_KEY_Home, XKB_KEY_KP_Home, // 4
		XKB_KEY_End, XKB_KEY_KP_End, XKB_KEY_F1, XKB_KEY_F2, XKB_KEY_F3, // 5
		XKB_KEY_F4, XKB_KEY_F5, XKB_KEY_F6, XKB_KEY_F7, XKB_KEY_F8, // 5
		XKB_KEY_F9, XKB_KEY_F10, XKB_KEY_F11, XKB_KEY_F12, XKB_KEY_0, // 5
		XKB_KEY_1, XKB_KEY_2, XKB_KEY_3, XKB_KEY_4, XKB_KEY_5, XKB_KEY_6, // 6
		XKB_KEY_7, XKB_KEY_8, XKB_KEY_9 // 3
	};

	if (!cancel_found && !state->args.hide_cancel)
		add_action(state, WL_ACTION_CANCEL,
				default_labels[WL_ACTION_CANCEL], default_symbols[WL_ACTION_CANCEL], NULL, NULL,
				default_shortcut_keys[WL_ACTION_CANCEL]);

	wl_list_for_each(action, &state->actions, link) {
		if (!action->command && action->type != WL_ACTION_CANCEL) {
			if (action->label)
				waylogout_log(LOG_ERROR,
						"Label configured but no command configured for action \"%s\".",
						default_labels[action->type]);
			else if (action->symbol[0] != '\0')
				waylogout_log(LOG_ERROR,
						"Symbol configured but no command configured for action \"%s\".",
						default_labels[action->type]);
			else if (action->shortcut.key != XKB_KEY_NoSymbol)
				waylogout_log(LOG_ERROR,
						"Keyboard shortcut configured but no command configured for action \"%s\".",
						default_labels[action->type]);
			return EXIT_FAILURE;
		}
		if (! action->label)
			action->label = strdup(default_labels[action->type]);
		if (action->symbol[0] == '\0')
			strncpy(action->symbol, default_symbols[action->type], 4);
		if ((action->shortcut.key == XKB_KEY_NoSymbol) && (!action->shortcut.deleted)) {
			action->shortcut = (struct waylogout_keycombo) {
				.key = default_shortcut_keys[action->type],
				.control = false,
				.alt = false,
				.deleted = false
			};
		} else {
			if (! (action->shortcut.control || action->shortcut.alt)) {
				for (uint8_t res_key_index = 0; res_key_index < N_RESERVED_KEYS; ++res_key_index) {
					if ((action->shortcut.key == reserved_keys[res_key_index]) &&
							((action->type != WL_ACTION_CANCEL) ||
							(action->shortcut.key != XKB_KEY_Escape))) {
						waylogout_log(LOG_ERROR,
								"Requested keyboard shortcut for action \"%s\" is reserved for navigation/selection/exit. Use a different key or add a modifier key.",
								default_labels[action->type]);
						action->shortcut.key = XKB_KEY_NoSymbol;
					}
				}
			}
			struct waylogout_action *compared_action;
			wl_list_for_each(compared_action, &state->actions, link) {
				if ((compared_action != action) && (compare_shortcuts(&action->shortcut, &compared_action->shortcut))) {
					waylogout_log(LOG_ERROR,
							"Keyboard shortcut collision for \"%s\" and \"%s\" actions. Removing shortcut for \"%s\".",
							default_labels[action->type], default_labels[compared_action->type], default_labels[compared_action->type]);
					compared_action->shortcut.key = XKB_KEY_NoSymbol;
					compared_action->shortcut.deleted = true;
				}
			}
		}
		char keyname[64];
		xkb_keysym_get_name(action->shortcut.key, keyname, 64);
		waylogout_log(LOG_DEBUG,
		  "Action %s:  \n"
		  "  symbol  %s\n"
		  "  command %s\n"
		  "  shortcut %s"
		  ,
		  action->label,
		  action->symbol,
		  action->command,
		  keyname
		);
	}

	waylogout_log(LOG_DEBUG, "Found %d configured actions", state->n_actions);

	return 0;

}
