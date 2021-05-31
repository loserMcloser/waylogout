# waylogout

waylogout is graphical logout/suspend/reboot/shutdown utility inspired by
[oblogout](https://launchpad.net/oblogout)
and based on code from
[swaylock-effects](https://github.com/mortie/swaylock-effects).

TODO screenshot

## Example Command TODO

	waylogout \
		--screenshots \
		--indicator-radius 100 \
		--indicator-thickness 7 \
		--effect-blur 7x5 \
		--effect-vignette 0.5:0.5 \
		--ring-color bb00cc \
		--key-hl-color 880033 \
		--line-color 00000000 \
		--inside-color 00000088 \
		--separator-color 00000000 \
		--fade-in 0.2

## Installation

### From Packages

* TODO AUR

### Compiling from Source

Install dependencies:

* meson \*
* wayland
* wayland-protocols \*
* libxkbcommon
* cairo
* gdk-pixbuf2 \*\*
* [scdoc](https://git.sr.ht/~sircmpwn/scdoc) (optional: man pages) \*
* git \*
* openmp (if using a compiler other than GCC)

_\*Compile-time dep_

_\*\*Optional: required for background images other than PNG_

Run these commands:

	meson build
	ninja -C build
	sudo ninja -C build install

## Effects

Similar to available effects in
[swaylock-effects](https://github.com/mortie/swaylock-effects).

### Custom

`--effect-custom <path>`: Load a custom effect from a shared object.

The .so must export a function `void waylogout_effect(uint32_t *data, int width, int height)`
or a function `uint32_t waylogout_pixel(uint32_t pix, int x, int y, int width, int height)`.
