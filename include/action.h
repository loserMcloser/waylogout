#ifndef _WAYLOGOUT_ACTION_H
#define _WAYLOGOUT_ACTION_H
#include "input.h"
#include "pool-buffer.h"

struct waylogout_state;

enum waylogout_action_type {
	WL_ACTION_NO_ACTION,
	WL_ACTION_POWEROFF,
	WL_ACTION_REBOOT,
	WL_ACTION_SUSPEND,
	WL_ACTION_HIBERNATE,
	WL_ACTION_LOGOUT,
	WL_ACTION_RELOAD,
	WL_ACTION_LOCK,
	WL_ACTION_SWITCH,
	WL_ACTION_CANCEL,
	WL_ACTION_END
};

struct waylogout_action {
	enum waylogout_action_type type;
	char *label;
	char symbol[8];
	char *command;
	uint8_t row;
	struct waylogout_keycombo shortcut;
	bool rendered_depressed;
	struct pool_buffer indicator_buffers[2];
	struct wl_list link;
};

struct waylogout_action_surface {
	struct waylogout_action *action;
	struct wl_surface *surface; // surface made into subsurface
	struct wl_subsurface *subsurface;
	struct waylogout_surface *parent_surface;
	uint32_t indicator_width, indicator_height;
};


void select_first_action(struct waylogout_state *state);
void select_last_action(struct waylogout_state *state);
void select_next_action(struct waylogout_state *state);
void select_prev_action(struct waylogout_state *state);

// pass keycombo == NULL to just set up the shortcut as key with no modifiers
void add_action(struct waylogout_state *state, enum waylogout_action_type type,
		const char *label, char *symbol, char *command, char* keycombo, xkb_keysym_t key);

void add_action_label(struct waylogout_state *state,
		enum waylogout_action_type type, char *label);
void add_action_shortcut(struct waylogout_state *state,
		enum waylogout_action_type type, char *keycombo);
void add_action_symbol(struct waylogout_state *state,
		enum waylogout_action_type type, char *symbol);
void add_action_command(struct waylogout_state *state,
		enum waylogout_action_type type, char *command);
void set_default_action(struct waylogout_state *state);
void run_action(struct waylogout_state *state, struct waylogout_action *action);
void setup_rows(struct waylogout_state *state);

struct waylogout_action *find_action(struct wl_list *actions, enum waylogout_action_type type);

int finish_actions_setup(struct waylogout_state *state);

#endif
