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

#include "../config.h"

struct led_config {
	struct libevdev *dev;

	char *device_path;
	char *led_path;

	int device_fd;
	int led_fd;

	uint16_t keyboard_led;

	int led_brightness_on;
	int led_brightness_off;

	char *led_brightness_on_str;
	size_t led_brightness_on_strlen;

	char *led_brightness_off_str;
	size_t led_brightness_off_strlen;
};

static struct led_config arg_led_config;

static uint16_t
parse_keyboard_led(const char *value,
		   GError **error) {
	uint16_t keyboard_led;

	if (strcasecmp(value, "capslock") == 0)
		keyboard_led = LED_CAPSL;
	else if (strcasecmp(value, "scrolllock") == 0)
		keyboard_led = LED_SCROLLL;
	else if (strcasecmp(value, "numlock") == 0 ||
		 strcasecmp(value, "numberlock") == 0)
		keyboard_led = LED_NUML;
	else {
		*error = g_error_new(G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
				     "Invalid value for keyboard-led");
		return 0;
	}

	return keyboard_led;
}

static gboolean
keyboard_led_option_cb(const char *option_name,
		       const char *value,
		       void *data,
		       GError **error) {
	arg_led_config.keyboard_led = parse_keyboard_led(value, error);

	return arg_led_config.keyboard_led;
}

static inline void
require_option(GOptionContext *context,
	       bool option,
	       const char *msg) {
	if (!option) {
		fprintf(stderr,
			"Error: %s\n"
			"%s",
			msg, g_option_context_get_help(context, false, NULL));
		exit(1);
	}
}

static void
print_version(const char *option_name,
	      const char *value,
	      void *data,
	      GError **error) {
	printf(PACKAGE_STRING "\n");
	exit(0);
}

static GOptionEntry options[] = {
	{ "version", 'V', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, print_version, "Print version", NULL },
	{ "input-device", 'i', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &arg_led_config.device_path, "Evdev device to monitor", "/dev/input/eventX" },
	{ "led-device", 'o', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &arg_led_config.led_path, "LED device to bind to", "/sys/class/leds/some_led" },
	{ "keyboard-led", 'l', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, keyboard_led_option_cb, "LED to emulate", "(caps|scroll|num[ber])lock" },
	{ "brightness-on", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &arg_led_config.led_brightness_on, "Brightness value when LED is on", "brightness" },
	{ "brightness-off", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &arg_led_config.led_brightness_off, "Brightness value when LED is off", "brightness" },
	{ NULL }
};

static void
update_led(struct led_config *config,
	   uint16_t value) {
	char *out;
	size_t out_len;

	if (value == 1) {
		out = config->led_brightness_on_str;
		out_len = config->led_brightness_on_strlen;
	}
	else {
		out = config->led_brightness_off_str;
		out_len = config->led_brightness_off_strlen;
	}

	if (write(config->led_fd, out, out_len) < 0) {
		fprintf(stderr,
			"Error: Failed to write to \"%s\": %s\n",
			config->led_path, strerror(errno));
		exit(1);
	}
}

static void
init_led_config(struct led_config *config) {
	int rc;

	if (!config->led_brightness_on)
		config->led_brightness_on = 1;

	config->led_brightness_on_str =
		g_strdup_printf("%d\n", config->led_brightness_on);
	config->led_brightness_on_strlen =
		strlen(config->led_brightness_on_str);

	config->led_brightness_off_str =
		g_strdup_printf("%d\n", config->led_brightness_off);
	config->led_brightness_off_strlen =
		strlen(config->led_brightness_off_str);

	config->device_fd = open(config->device_path, O_RDONLY);
	if (config->device_fd == -1) {
		fprintf(stderr, "Failed to open %s: %s\n",
			config->device_path, strerror(errno));
		exit(1);
	}

	rc = libevdev_new_from_fd(config->device_fd, &config->dev);
	if (rc < 0) {
		fprintf(stderr, "Failed to initialize libevdev: %s\n",
			strerror(rc));
		exit(1);
	}

	config->led_path =
		g_realloc(config->led_path,
			  strlen(config->led_path) + sizeof("/brightness"));
	strcat(config->led_path, "/brightness");

	config->led_fd = open(config->led_path, O_WRONLY);
	if (config->led_fd == -1) {
		fprintf(stderr, "Failed to open %s: %s\n",
			config->led_path, strerror(errno));
		exit(1);
	}
}

int
main(int argc, char *argv[]) {
	GError *error = NULL;
	GOptionContext *option_context;

	option_context = g_option_context_new("- map keyboard LED to another LED");
	g_option_context_add_main_entries(option_context, options, NULL);
	g_option_context_set_description(option_context,
		"keyledd is a daemon intended to help users who have no LED for a certain\n"
		"functionality on their keyboard (for example, a caps lock led), by \n"
		"allowing them to remap a different LED on their computer to mimic the \n"
		"missing LED's behavior.\n"
		"\n"
		"License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n"
		"\n"
		"Written by Lyude <" PACKAGE_BUGREPORT "> (C) 2015\n"
		"You can find the project page here: " PACKAGE_URL
		);

	if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
		fprintf(stderr, "Invalid options: %s\n", error->message);
		exit(1);
	}

	require_option(context, arg_led_config.device_path,
		       "No input device specified");
	require_option(context, arg_led_config.led_path,
		       "No LED device specified");
	require_option(context, arg_led_config.keyboard_led,
		       "No keyboard LED specified");

	init_led_config(&arg_led_config);

	g_option_context_free(context);

	/* Update the LED to the current state of the LED */
	update_led(&arg_led_config,
		   libevdev_get_event_value(arg_led_config.dev, EV_LED,
					    arg_led_config.keyboard_led));

	while (true) {
		struct input_event ev;
		int rc;

		rc = libevdev_next_event(arg_led_config.dev,
					 LIBEVDEV_READ_FLAG_BLOCKING |
					 LIBEVDEV_READ_FLAG_NORMAL,
					 &ev);
		if (rc != 0)
			break;

		if (ev.type == EV_LED && ev.code == arg_led_config.keyboard_led)
			update_led(&arg_led_config, ev.value);
	}

	return 0;
}
