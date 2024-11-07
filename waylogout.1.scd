waylogout(1)

# NAME

waylogout - Graphical logout/suspend/poweroff/etc utility for Wayland

# SYNOPSIS

_waylogout_ [options...]

Provides you with graphical set of options to logout/suspend/poweroff/etc in your Wayland session.

# OPTIONS

*-C, --config* <path>
	The config file to use. By default, the following paths are checked:
	_$HOME/.waylogout/config_, _$XDG\_CONFIG\_HOME/waylogout/config_, and
	_SYSCONFDIR/waylogout/config_. All flags aside from this one are valid
	options in the configuration file using the format _long-option=value_.
	For options such as _logout-command_, just supply the _long-option_.
	All leading dashes should be omitted and the equals sign is required for
	flags that take an argument.

*-d, --debug*
	Enable debugging output.

*--fade-in* <seconds>
	Fade in the logout screen.

*-h, --help*
	Show help message and quit.

*-v, --version*
	Show the version number and quit.

*--scroll-sensitivity* <amount>
	How fast selected action will change when scrolling with mouse/touch. Lower is faster; default is 8.

*--instant-run*
	Instantly run actions on key press, without confirmation with enter key.

*--poweroff-command* <command>
	Command to run when the _poweroff_ action is activated.

*--reboot-command* <command>
	Command to run when the _reboot_ action is activated.

*--suspend-command* <command>
	Command to run when the _suspend_ action is activated

*--hibernate-command* <command>
Command to run when the _hibernate_ action is activated.

*--logout-command* <command>
	Command to run when the _logout_ action is activated.

*--reload-command* <command>
	Command to run when the _reload session config_ action is activated.

*--lock-command* <command>
	Command to run when the _lock_ action is activated.

*--switch-user-command* <command>
	Command to run when the _switch user_ action is activated.

*--default-action* <action-name>
	Action to pre-select on start-up. Must be one of _poweroff_, _reboot_, _suspend_, _hibernate_, _logout_, _reload_, _lock_, or _switch-user_.

# APPEARANCE

Action indicators will only appear for actions where a command has been specified, and will be displayed in the order they are configured (on the command line or in the config file).

*--poweroff-label* <label>
	Label to display in the _poweroff_ action indicator.
	Default: "power off"

*--poweroff-symbol* <character>
	Symbol character to display in the _poweroff_ action indicator.
	Default: 

*--reboot-label* <label>
	Label to display in the _reboot_ action indicator.
	Default: "reboot"

*--reboot-symbol* <character>
	Symbol character to display in the _reboot_ action indicator.
	Default: 

*--suspend-label* <label>
	Label to display in the _suspend_ action indicator.
	Default: "sleep"

*--suspend-symbol* <character>
	Symbol character to display in the _suspend_ action indicator.
	Default: 

*--hibernate-label* <label>
	Label to display in the _hibernate_ action indicator.
	Default: "hibernate"

*--hibernate-symbol* <character>
	Symbol character to display in the _hibernate_ action indicator.
	Default: 

*--logout-label* <label>
	Label to display in the _logout_ action indicator.
	Default: "logout"

*--logout-symbol* <character>
	Symbol character to display in the _logout_ action indicator.
	Default: 

*--reload-label* <label>
	Label to display in the _reload session config_ action indicator.
	Default: "reload wm"

*--reload-symbol* <character>
	Symbol character to display in the _reload session config_ action indicator.
	Default: 

*--lock-label* <label>
	Label to display in the _lock_ action indicator.
	Default: "lock"

*--lock-symbol* <character>
	Symbol character to display in the _lock_ action indicator.
	Default: 

*--switch-user-label* <label>
	Label to display in the _switch user_ action indicator.
	Default: "switch user"

*--switch-user-symbol* <character>
	Symbol character to display in the _switch user_ action indicator.
	Default: 

*--cancel-label* <label>
	Label to display in the _cancel_ action indicator.
	Default: "switch user"

*--cancel-symbol* <character>
	Symbol character to display in the _cancel_ action indicator.
	Default: 

*--hide-cancel*
	Hide the cancel action.

*-i, --image* [[<output>]:]<path>
	Display the given image, optionally only on the given output. Use -c to set
	a background color. If the path potentially contains a ':', prefix it with another
	':' to prevent interpreting part of it as <output>.

*-l, --labels*
	Always show action labels.

*-R, --rows <number>*
	Number of rows of action indicators to display.
	Default: 1.

*--selection-label*
	Show action label for selected action,
	even when --labels is not specified.

*-S, --screenshots*
	Display a screenshot.

*-s, --scaling*
	Image scaling mode: _stretch_, _fill_, _fit_, _center_, _tile_,
	_solid\_color_. Use _solid\_color_ to display only the background color, even
	if a background image is specified.

*-t, --tiling*
	Same as --scaling=tile.

*-c, --color* <rrggbb[aa]>
	Turn the screen into the given color instead of white. If -i is used, this
	sets the background of the image to the given color. Defaults to white
	(FFFFFF).

*--font* <font>
	Sets the font of the action label text.

*--fa-font* <font>
	Sets the name of the Font Awesome font. Default is "Font Awesome 6 Free".

*--label-font-size* <size>
	Sets a fixed font size for the action label text.

*--symbol-font-size* <size>
	Sets a fixed font size for the action symbols.

*--indicator-radius* <radius>
	Sets the action indicator radius. The default value is 50.

*--indicator-thickness* <thickness>
	Sets the action indicator thickness. The default value is 10.

*--indicator-x-position* <x>
	Sets the horizontal centre position of the action indicator array.

*--indicator-y-position* <y>
	Sets the vertical centre position of the action indicator array.

*--indicator-separation* <sep>
	Sets a fixed amount of space separating action indicators.

*--inside-color* <rrggbb[aa]>
	Sets the color of the inside of the indicator.

*--inside-selection-color* <rrggbb[aa]>
	Sets the color of the inside of the selected action indicator.

*--line-color* <rrggbb[aa]>
	Sets the color of the line between the inside and ring.

*--line-selection-color* <rrggbb[aa]>
	Sets the color of the line between the inside and ring in the selected action indicator.

*-n, --line-uses-inside*
	Use the inside color for the line between the inside and ring.

*-r, --line-uses-ring*
	Use the ring color for the line between the inside and ring.

*--ring-color* <rrggbb[aa]>
	Sets the color of the ring of the action indicators.

*--ring-selection-color* <rrggbb[aa]>
	Sets the color of the ring of the selected action indicator.

*--text-color* <rrggbb[aa]>
	Sets the color of the text.

*--text-selection-color* <rrggbb[aa]>
	Sets the color of the text for the selected action indicator.

*--effect-blur* <radius>x<times>
	Blur displayed images.

*--effect-pixelate* <factor>
	Pixelate displayed images.

*--effect-scale* <scale>
	Scale the image by a factor. This can be used to
	make other effects faster if you don't need the full resolution.

*--effect-greyscale*
	Make the displayed image greyscale.

*--effect-vignette* <base>:<factor>
	Apply a vignette effect to images.
	Base and factor should be numbers between 0 and 1.

*--effect-compose* <position>;<size>;<gravity>;<path>
	Overlay another image to your logout screen.
	The _position_, _size_ and _gravity_ part is optional.

*--effect-custom* <path>
	Load a custom effect from a shared object. The .so must export a++
*void waylogout_effect(uint32\_t \*data, int width, int height, int scale)*++
or an *uint32\_t waylogout_pixel(uint32\_t pix, int x, int y, int width, int height)*.

*--time-effects*
	Measure the time it takes to run each effect.

# NAVIGATION

You may select a particular action by navigating with the arrow keys, home/end, tab/shift-tab, the number keys, or the mouse scroll wheel, or by hovering the mouse pointer.

To activate the configured command for the selected action, press enter, or click the mouse (while hovering), or middle-click the mouse (anywhere).

# SHORTCUT KEYS

Shortcut keys can be customized --- see _SHORTCUT KEY OPTIONS_ below.

Here are the defaults.

*Escape*
	Exit without running any action command.

*c*
	Select the reload session config action (if configured).

*h*
	Select the hibernate action (if configured).

*k*
	Select the lock action (if configured).

*p*
	Select the poweroff action (if configured).

*r*
	Select the reboot action (if configured).

*s*
	Select the suspend action (if configured).

*u*
	Select the switch-user action (if configured).

*x*
	Select the logout action (if configured).


# SHORTCUT KEY OPTIONS

Keyboard shortcuts can be customized with the following options.

Recognized modifiers are _Ctrl_ and _Alt_. To indicate a shifted key, just use the upper case version of the letter. For example, a keycombo option of "Ctrl+k" means typing the K key while holding down _Ctrl_, while "Ctrl+K" means tying the K key while holding down both _Ctrl_ and _Shift_.

The two modifiers can be combined: for example, "Ctrl+Alt+k".

For special keys you must use their keysym names --- see _xkbcommon-keysyms.h_ and use the last part of a defined macro name as the key name.

*--poweroff-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the _poweroff_ action.

*--reboot-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the _reboot_ action.

*--suspend-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the _suspend_ action.

*--hibernate-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the _hibernate_ action.

*--logout-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the _logout_ action.

*--reload-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the _reload_ action.

*--lock-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the _lock_ action.

*--switch-user-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the _switch user_ action.

*--cancel-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the _cancel_ action.

*--reverse-arrows*
	Reverse the direction of the up/down arrows.


# CUSTOM ACTIONS

You can add your own custom actions using the options below. These options can be specified multiple times, but in that case should be grouped together, with each grouping containing at least a *custom-command*.

*--custom-command* <command>
	Command to run for the custom action.

*--custom-label* <label>
	Label to display for the custom action.

*--custom-symbol* <character>
	Symbol character to display for the custom action.

*--custom-shortcut* <keycombo>
	Keyboard shortcut key (with optional modifiers) to select the custom action.


# AUTHORS

Written by Jeremy Sylvestre, based on the codebase of Jakub Jirutka's fork of swaylock-effects by Martin Dørum, which is in turn forked from swaylock by Drew DeVault.

For more information about waylogout development, see
https://github.com/loserMcloser/waylogout.
