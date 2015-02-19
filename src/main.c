/* ©2015 Stephen Chandler Paul <thatslyude@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.

 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>
#include <libevdev/libevdev.h>

static char *device_path;
static char *led_path;
static uint16_t keyboard_led;

static int led_brightness_on,
	   led_brightness_off;

static char *led_brightness_on_str,
	    *led_brightness_off_str;
static size_t led_brightness_on_strlen,
	      led_brightness_off_strlen;

static bool
parse_keyboard_led(const char *value,
		   GError **error) {
	if (strcasecmp(value, "caps_lock") == 0)
		keyboard_led = LED_CAPSL;
	else if (strcasecmp(value, "scroll_lock") == 0)
		keyboard_led = LED_SCROLLL;
	else if (strcasecmp(value, "num_lock") == 0 ||
		 strcasecmp(value, "number_lock") == 0)
		keyboard_led = LED_NUML;
	else {
		*error = g_error_new(G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
				     "Invalid value for keyboard-led");
		return false;
	}

	return true;
}

static gboolean
keyboard_led_option_cb(const char *option_name,
		       const char *value,
		       void *data,
		       GError **error) {
	return parse_keyboard_led(value, error);
}

static inline void
require_option(GOptionContext *context,
	       bool option,
	       const char *msg) {
	if (!option) {
		fprintf(stderr,
			"Error: %s\n"
			"%s\n",
			msg, g_option_context_get_help(context, false, NULL));
		exit(1);
	}
}

static GOptionEntry options[] = {
	{ "input-device", 'i', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &device_path, "Evdev device to monitor", "/dev/input/eventX" },
	{ "led-device", 'o', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &led_path, "LED device to bind to", "/sys/class/leds/some_led" },
	{ "keyboard-led", 'l', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, keyboard_led_option_cb, "LED to emulate", "(caps|scroll|num[ber])_lock" },
	{ "brightness-on", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &led_brightness_on, "Brightness value when LED is on", "brightness" },
	{ "brightness-off", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &led_brightness_off, "Brightness value when LED is off", "brightness" },
	{ NULL }
};

static void
update_led(int led_fd,
	   uint16_t value) {
	char *out;
	size_t out_len;

	if (value == 1) {
		out = led_brightness_on_str;
		out_len = led_brightness_on_strlen;
	}
	else {
		out = led_brightness_off_str;
		out_len = led_brightness_off_strlen;
	}

	if (write(led_fd, out, out_len) < 0) {
		fprintf(stderr,
			"Error: Failed to write to \"%s\": %s\n",
			led_path, strerror(errno));
		exit(1);
	}
}

int
main(int argc, char *argv[]) {
	struct libevdev *dev = NULL;
	GError *error = NULL;
	GOptionContext *context;
	int device_fd, led_fd;
	int rc;

	context = g_option_context_new("- map keyboard LED to another LED");
	g_option_context_add_main_entries(context, options, NULL);

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		fprintf(stderr, "Invalid options: %s\n", error->message);
		exit(1);
	}

	require_option(context, device_path,
		       "No input device specified");
	require_option(context, led_path,
		       "No LED device specified");
	require_option(context, keyboard_led,
		       "No keyboard LED specified");
	require_option(context, led_brightness_on,
		       "No brightness on value specified");

	device_fd = open(device_path, O_RDONLY);
	if (device_fd == -1) {
		fprintf(stderr, "Failed to open %s: %s\n",
			device_path, strerror(errno));
		exit(1);
	}

	led_path = g_realloc(led_path, strlen(led_path) + sizeof("/brightness"));
	strcat(led_path, "/brightness");

	led_fd = open(led_path, O_WRONLY);
	if (led_fd == -1) {
		fprintf(stderr, "Failed to open %s: %s\n",
			led_path, strerror(errno));
		exit(1);
	}

	rc = libevdev_new_from_fd(device_fd, &dev);
	if (rc < 0) {
		fprintf(stderr, "Failed to initialize libevdev: %s\n",
			strerror(rc));
		exit(1);
	}

	led_brightness_on_str = g_strdup_printf("%d\n", led_brightness_on);
	led_brightness_on_strlen = strlen(led_brightness_on_str);

	led_brightness_off_str = g_strdup_printf("%d\n", led_brightness_off);
	led_brightness_off_strlen = strlen(led_brightness_off_str);

	while (true) {
		struct input_event ev;

		rc = libevdev_next_event(dev,
					 LIBEVDEV_READ_FLAG_BLOCKING |
					 LIBEVDEV_READ_FLAG_NORMAL,
					 &ev);
		if (rc != 0)
			break;

		if (ev.type == EV_LED && ev.code == keyboard_led)
			update_led(led_fd, ev.value);
	}

	return 0;
}
