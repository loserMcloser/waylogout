# waylogout

waylogout is graphical logout/suspend/reboot/shutdown dialog for wayland.
It is inspired by
[oblogout](https://launchpad.net/oblogout)
and based on code from
[swaylock-effects](https://github.com/mortie/swaylock-effects).

![Screenshot](/screenshot.png)

## Example Command

This is the command used to make the screenshot.

	waylogout \
		--hide-cancel \
		--screenshots \
		--font="Baloo 2" \
		--effect-blur=7x5 \
		--indicator-thickness=20 \
		--ring-color=888888aa \
		--inside-color=88888866 \
		--text-color=eaeaeaaa \
		--line-color=00000000 \
		--ring-selection-color=33cc33aa \
		--inside-selection-color=33cc3366 \
		--text-selection-color=eaeaeaaa \
		--line-selection-color=00000000 \
		--lock-command="echo lock" \
		--logout-command="echo logout" \
		--suspend-command="echo suspend" \
		--hibernate-command="echo hibernate" \
		--poweroff-command="echo poweroff" \
		--reboot-command="echo reboot" \
		--switch-user-command="echo switch" \
		--selection-label

## Installation

### From Packages

* Arch Linux (AUR): [waylogout-git](https://aur.archlinux.org/packages/waylogout-git)

### Compiling from Source

Install dependencies:

* meson \*
* wayland
* wayland-protocols \*
* libxkbcommon
* cairo
* gdk-pixbuf2 \*\*
* [scdoc](https://git.sr.ht/~sircmpwn/scdoc) \*\*\*
* git \*
* openmp (if using a compiler other than GCC)
* Font Awesome 5 Free

_\*Compile-time dep_

_\*\*Optional: required for background images other than PNG_

_\*\*\*Optional: man pages_

Run these commands:

	meson build
	ninja -C build
	sudo ninja -C build install

## Effects

See the description of available effects in
[swaylock-effects](https://github.com/mortie/swaylock-effects).
