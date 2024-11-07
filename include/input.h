#ifndef _WAYLOGOUT_INPUT_H
#define _WAYLOGOUT_INPUT_H
#include <xkbcommon/xkbcommon.h>
#include <stdbool.h>

struct waylogout_keycombo {
	bool control;
	bool alt;
	bool deleted;
	xkb_keysym_t key;
};

bool compare_shortcuts(struct waylogout_keycombo *combo1, struct waylogout_keycombo *combo2);
void parse_shortcut(char *keycombo, struct waylogout_keycombo *shortcut);

#endif
