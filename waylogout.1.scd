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

*--poweroff-command* <command>
	Command to run when the poweroff action is activated.

*--reboot-command* <command>
	Command to run when the reboot action is activated.

*--suspend-command* <command>
	Command to run when the suspend action is activated

*--hibernate-command* <command>
Command to run when the hibernate action is activated.

*--logout-command* <command>
	Command to run when the logout action is activated.

*--lock-command* <command>
	Command to run when the lock action is activated.

*--switch-user-command* <command>
	Command to run when the switch-user action is activated.

# APPEARANCE

Action indicators will only appear for actions where a command has been specified, and will be displayed in the order their commands are specified.

*--hide-cancel*
	Hide the cancel action.

*-i, --image* [[<output>]:]<path>
	Display the given image, optionally only on the given output. Use -c to set
	a background color. If the path potentially contains a ':', prefix it with another
	':' to prevent interpreting part of it as <output>.

*-l, --labels*
	Always show action labels.

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
	Overlay another image to your lock screen.
	The _position_, _size_ and _gravity_ part is optional.

*--effect-custom* <path>
	Load a custom effect from a shared object. The .so must export a++
*void waylogout_effect(uint32\_t \*data, int width, int height, int scale)*++
or an *uint32\_t waylogout_pixel(uint32\_t pix, int x, int y, int width, int height)*.

*--time-effects*
	Measure the time it takes to run each effect.

# NAVIGATION

You may select a particular action by navigating with the arrow keys, the number keys, tab/shift-tab, or the mouse scroll wheel, or by hovering the mouse pointer.

To activate the configured command for the selected action, press enter, or click the mouse (while hovering), or middle-click the mouse (anywhere).

# SHORTCUT KEYS

*Esc*
	Exit without running any action command.

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

*w*
	Select the switch-user action (if configured).

*x*
	Select the logout action (if configured).


# SHORTCUT KEY OPTIONS

*--reverse-arrows*
	Reverse the direction of the up/down arrows.


# AUTHORS

Written by Jeremy Sylvestre, based on the codebase of swaylock-effects by Martin Dørum, which is in turn forked from upstream Swaylock by Drew DeVault.

For more information about waylogout development, see
https://github.com/loserMcloser/waylogout.
