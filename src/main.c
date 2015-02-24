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
	char *name;

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

static GHashTable *led_configs;
static GHashTable *evdev_fds;
static GArray *led_poll_fds;
static char *config_file_path = SYSCONFDIR "/keyledd.conf";

#define KEYLEDD_ERROR (g_quark_from_static_string("keyledd-error-domain"))

enum keyledd_error {
	KEYLEDD_ERROR_KEYBOARD_LED_TAKEN,
	KEYLEDD_ERROR_SYSTEM_LED_TAKEN,
	KEYLEDD_ERROR_NO_LEDS_DEFINED
};

/* Hash keys for keyboard led/device pairs look like this:
 *
 * 0-32: Device's fd
 * 32-48: LED code
 */
static inline int64_t
get_led_config_hash_key_from_values(int device_fd,
				    uint16_t keyboard_led) {
	int64_t key;

	key = device_fd;
	key |= ((int64_t)keyboard_led << 32);

	return key;
}

static inline int64_t
get_led_config_hash_key_from_config(struct led_config *config) {
	return get_led_config_hash_key_from_values(config->device_fd,
						   config->keyboard_led);
}

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
				     "Invalid value for KeyboardLed");
		return 0;
	}

	return keyboard_led;
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
	{ "config-file", 'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &config_file_path, "Path to config file", "<file>" },
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

static bool
init_led_config(struct led_config *config,
		GError **error) {
	int rc;
	bool device_already_opened = false;
	GList *keys = g_hash_table_get_keys(led_configs);
	int64_t *hash_key = g_new(int64_t, 1);

	if (!config->led_brightness_on)
		config->led_brightness_on = 1;

	/* Check to make sure this config doesn't conflict with any of the
	 * others */
	for (GList *l = keys; l != NULL; l = l->next) {
		struct led_config *c = l->data;

		if (strcmp(config->device_path, c->device_path) == 0) {
			if (!device_already_opened) {
				g_free(c->device_path);

				c->device_path = config->device_path;
				c->device_fd = config->device_fd;
				c->dev = config->dev;

				device_already_opened = true;
			}

			if (config->keyboard_led == c->keyboard_led) {
			       *error = g_error_new(
				    KEYLEDD_ERROR,
				    KEYLEDD_ERROR_KEYBOARD_LED_TAKEN,
				    "%s: Keyboard code already taken by %s",
				    config->name, c->name);

			       return false;
			}
		}

		if (strcmp(config->led_path, c->led_path) == 0) {
			*error = g_error_new(KEYLEDD_ERROR,
					     KEYLEDD_ERROR_SYSTEM_LED_TAKEN,
					     "%s: LED \"%s\" already taken by %s",
					     config->name, config->led_path,
					     c->name);

			return false;
		}

		/* We're already checking all of the current groups, so why not
		 * share led brightness strings/string lengths while we're at it
		 */
		if (c->led_brightness_on == c->led_brightness_off) {
			config->led_brightness_on_str =
				c->led_brightness_on_str;
			config->led_brightness_on_strlen =
				c->led_brightness_on_strlen;
		}

		if (c->led_brightness_off == c->led_brightness_off) {
			config->led_brightness_off_str =
				c->led_brightness_off_str;
			config->led_brightness_off_strlen =
				c->led_brightness_off_strlen;
		}
	}

	if (!config->led_brightness_on_str) {
		config->led_brightness_on_str =
			g_strdup_printf("%d\n", config->led_brightness_on);
		config->led_brightness_on_strlen =
			strlen(config->led_brightness_on_str);
	}

	if (!config->led_brightness_off_str) {
		config->led_brightness_off_str =
			g_strdup_printf("%d\n", config->led_brightness_off);
		config->led_brightness_off_strlen =
			strlen(config->led_brightness_off_str);
	}

	if (!device_already_opened) {
		GPollFD poll_fd;

		config->device_fd = open(config->device_path,
					 O_RDONLY | O_NONBLOCK);
		if (config->device_fd == -1) {
			fprintf(stderr, "Failed to open %s for \"%s\": %s\n",
				config->device_path, config->name,
				strerror(errno));
			exit(1);
		}

		rc = libevdev_new_from_fd(config->device_fd, &config->dev);
		if (rc < 0) {
			fprintf(stderr,
				"Failed to initialize libevdev for \"%s\": %s\n",
				config->name, strerror(rc));
			exit(1);
		}

		g_hash_table_insert(evdev_fds, &config->device_fd, config->dev);

		poll_fd.fd = config->device_fd;
		poll_fd.events = G_IO_IN;
		g_array_append_val(led_poll_fds, poll_fd);
	}

	config->led_path =
		g_realloc(config->led_path,
			  strlen(config->led_path) + sizeof("/brightness"));
	strcat(config->led_path, "/brightness");

	config->led_fd = open(config->led_path, O_WRONLY);
	if (config->led_fd == -1) {
		fprintf(stderr, "Failed to open %s for \"%s\": %s\n",
			config->led_path, config->name, strerror(errno));
		exit(1);
	}

	*hash_key = get_led_config_hash_key_from_config(config);
	g_hash_table_insert(led_configs, hash_key, config);

	return true;
}

static bool
parse_conf_file(GError **error) {
	GKeyFile *key_file = g_key_file_new();
	char *key_file_data;
	char **groups;
	size_t groups_len;

	g_return_val_if_fail(
	    g_file_get_contents(config_file_path, &key_file_data, NULL, error),
	    false);
	g_return_val_if_fail(
	    g_key_file_load_from_data(key_file, key_file_data, -1,
				      G_KEY_FILE_NONE, error),
	    false);

	g_free(key_file_data);

	groups = g_key_file_get_groups(key_file, &groups_len);

	if (groups_len == 0) {
		*error = g_error_new(KEYLEDD_ERROR,
				     KEYLEDD_ERROR_NO_LEDS_DEFINED,
				     "No LEDs defined in %s",
				     config_file_path);
		return false;
	}

	for (int i = 0; groups[i] != NULL; i++) {
		struct led_config *config = g_new0(struct led_config, 1);
		char *keyboard_led_str;

		config->name = groups[i];

		keyboard_led_str = g_key_file_get_string(key_file, groups[i],
							 "KeyboardLed", error);
		g_return_val_if_fail(keyboard_led_str != NULL, false);

		config->keyboard_led = parse_keyboard_led(keyboard_led_str,
							  error);
		g_return_val_if_fail(config->keyboard_led != 0, false);

		config->device_path = g_key_file_get_string(key_file, groups[i],
							    "InputDevice",
							    error);
		g_return_val_if_fail(config->device_path != NULL, false);

		config->led_path = g_key_file_get_string(key_file, groups[i],
							 "LedDevice", error);
		g_return_val_if_fail(config->led_path != NULL, false);

		config->led_brightness_on =
			g_key_file_get_integer(key_file, groups[i],
					       "BrightnessOn", error);
		g_return_val_if_fail(
		    config->led_brightness_on ||
		    g_error_matches(*error, G_KEY_FILE_ERROR,
				    G_KEY_FILE_ERROR_KEY_NOT_FOUND),
		    false);

		if (error) {
			g_error_free(*error);
			*error = NULL;
		}

		config->led_brightness_off =
			g_key_file_get_integer(key_file, groups[i],
					       "BrightnessOff", error);
		g_return_val_if_fail(
		    config->led_brightness_off ||
		    g_error_matches(*error, G_KEY_FILE_ERROR,
				    G_KEY_FILE_ERROR_KEY_NOT_FOUND),
		    false);

		if (error) {
			g_error_free(*error);
			*error = NULL;
		}

		g_return_val_if_fail(init_led_config(config, error), false);

		g_free(keyboard_led_str);
	}

	g_key_file_free(key_file);
	g_strfreev(groups);

	return true;
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

	g_option_context_free(option_context);

	led_poll_fds = g_array_new(false, true, sizeof(GPollFD));
	led_configs = g_hash_table_new(g_int64_hash, g_int64_equal);
	evdev_fds = g_hash_table_new(g_int_hash, g_int_equal);

	if (!parse_conf_file(&error)) {
		fprintf(stderr, "Config file error: %s\n",
			error->message);
		exit(1);
	}

	/* Initialize all of the LEDs to match their keyboard LEDs state */
	{
		GList *configs = g_hash_table_get_values(led_configs);

		for (GList *l = configs; l != NULL; l = l->next) {
			struct led_config *config = l->data;
			char *out_data;
			size_t out_len;

			if (libevdev_get_event_value(config->dev, EV_LED,
						     config->keyboard_led) == 1) {
				out_data = config->led_brightness_on_str;
				out_len = config->led_brightness_on_strlen;
			}
			else {
				out_data = config->led_brightness_off_str;
				out_len = config->led_brightness_off_strlen;
			}

			if (write(config->led_fd, out_data, out_len) == -1) {
				fprintf(stderr, "Couldn't write to %s: %s\n",
					config->led_path, strerror(errno));
				exit(1);
			}
		}

		g_list_free(configs);
	}

	while (g_poll((GPollFD*)led_poll_fds->data, led_poll_fds->len, -1) != -1) {
		for (unsigned int i = 0; i < led_poll_fds->len; i++) {
			GPollFD *c = &g_array_index(led_poll_fds, GPollFD, i);
			struct libevdev *dev;
			struct input_event ev;
			struct led_config *config;
			int rc;

			if (!c->revents)
				continue;

			dev = g_hash_table_lookup(evdev_fds, &c->fd);

			while (libevdev_has_event_pending(dev)) {
				int64_t hash_key;
				char *out_data;
				size_t out_len;

				rc = libevdev_next_event(
				    dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

				if (G_UNLIKELY(rc != 0)) {
					fprintf(stderr,
						"Error: %s\n",
						strerror(rc));
					exit(1);
				}

				if (ev.type != EV_LED)
					continue;

				hash_key = get_led_config_hash_key_from_values(
				    c->fd, ev.code);
				config = g_hash_table_lookup(led_configs,
							     &hash_key);

				if (!config)
					continue;

				if (ev.value == 1) {
					out_data = config->led_brightness_on_str;
					out_len = config->led_brightness_on_strlen;
				}
				else {
					out_data = config->led_brightness_off_str;
					out_len = config->led_brightness_off_strlen;
				}

				if (G_UNLIKELY(write(config->led_fd, out_data,
						     out_len) == -1)) {
					fprintf(stderr,
						"Couldn't write to %s: %s\n",
						config->led_path,
						strerror(errno));
					exit(1);
				}
			}
		}
	}

	return 0;
}
