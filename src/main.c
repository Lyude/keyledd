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
#include <gio/gio.h>
#include <glib-unix.h>

#include "../config.h"

struct led_config {
	char *name;

	struct libevdev *dev;

	char *device_path;
	char *led_path;

	GIOChannel *input_device;
	GIOChannel *led_device;

	uint16_t keyboard_led;

	int led_brightness_on;
	int led_brightness_off;

	char *led_brightness_on_str;
	size_t led_brightness_on_strlen;

	char *led_brightness_off_str;
	size_t led_brightness_off_strlen;
};

static GHashTable *led_configs;
static char *config_file_path = SYSCONFDIR "/keyledd.conf";

#ifdef WITH_SYSV_STYLE_INIT
static char *pid_file_path = NULL;
#endif /* WITH_SYSV_STYLE_INIT */

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
get_led_config_hash_key(int device_fd,
			uint16_t keyboard_led) {
	int64_t key;

	key = device_fd;
	key |= ((int64_t)keyboard_led << 32);

	return key;
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

#ifdef WITH_SYSTEMD
	printf("Compiled with systemd support? yes\n");
#else
	printf("Compiled with systemd support? no\n");
#endif /* WITH_SYSTEMD */

#ifdef WITH_SYSV_STYLE_INIT
	printf("Compiled with sysv-style init support? yes\n");
#else
	printf("Compiled with sysv-style init support? no\n");
#endif /* WITH_SYSV_STYLE_INIT */
	exit(0);
}

static GOptionEntry options[] = {
	{ "version", 'V', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, print_version, "Print version", NULL },
	{ "config-file", 'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &config_file_path, "Path to config file", "<file>" },

#ifdef WITH_SYSV_STYLE_INIT
	{ "pid-file", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &pid_file_path, "Path to PID file", "<pid-file>" },
#endif /* WITH_SYSV_STYLE_INIT */

	{ NULL }
};

static void
init_led_states() {
	GList *configs = g_hash_table_get_values(led_configs);
	GError *error = NULL;

	g_debug("Initializing LED states");

	for (GList *l = configs; l != NULL; l = l->next) {
		struct led_config *config = l->data;
		char *out_data;
		size_t out_len;

		if (libevdev_get_event_value(config->dev, EV_LED,
					     config->keyboard_led) == 1) {
			out_data = config->led_brightness_on_str;
			out_len = config->led_brightness_on_strlen;
			g_debug("%s: Setting %s to on (%d)",
				config->name, config->led_path, config->led_brightness_on);
		}
		else {
			out_data = config->led_brightness_off_str;
			out_len = config->led_brightness_off_strlen;
			g_debug("%s: Setting %s to off (%d)",
				config->name, config->led_path, config->led_brightness_off);
		}

		if (g_io_channel_write_chars(config->led_device,
					     out_data, out_len, NULL,
					     &error) == G_IO_STATUS_ERROR) {
			fprintf(stderr, "Couldn't write to %s: %s\n",
				config->led_path, error->message);
			exit(1);
		}
	}

	g_list_free(configs);
}

#ifdef WITH_SYSTEMD
static void
prepare_for_sleep(GDBusConnection *connection,
		  const char *sender_name,
		  const char *object_path,
		  const char *interface_name,
		  const char *signal_name,
		  GVariant *parameters,
		  gpointer user_data) {
	GVariant *going_to_sleep =
		g_variant_get_child_value(parameters, 0);

	if (!g_variant_get_boolean(going_to_sleep))
		init_led_states();

	g_variant_unref(going_to_sleep);
}
#endif /* WITH_SYSTEMD */

static gboolean
update_led(GIOChannel *source,
	   GIOCondition condition,
	   gpointer data) {
	struct libevdev *dev = data;
	struct input_event ev;
	int dev_fd;
	struct led_config *config;
	GError *error = NULL;

	dev_fd = g_io_channel_unix_get_fd(source);

	while (libevdev_has_event_pending(dev)) {
		int64_t hash_value;
		char *out_data;
		size_t out_len;
		GIOStatus rc;

		libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

		if (G_LIKELY(ev.type != EV_LED))
			continue;

		hash_value = get_led_config_hash_key(dev_fd, ev.code);
		config = g_hash_table_lookup(led_configs, &hash_value);

		if (!config)
			continue;

		if (ev.value == 1) {
			out_data = config->led_brightness_on_str;
			out_len = config->led_brightness_on_strlen;
			g_debug("%s: Setting %s to on (%d)",
				config->name, config->led_path, config->led_brightness_on);
		}
		else {
			out_data = config->led_brightness_off_str;
			out_len = config->led_brightness_off_strlen;
			g_debug("%s: Setting %s to off (%d)",
				config->name, config->led_path, config->led_brightness_off);
		}

		rc = g_io_channel_write_chars(config->led_device, out_data,
					      out_len, NULL, &error);
		if (G_UNLIKELY(rc != G_IO_STATUS_NORMAL)) {
			fprintf(stderr,
				"Couldn't write to LED device for %s: %s\n",
				config->name, error->message);
			exit(1);
		}
	}

	return true;
}

static gboolean
io_channel_failed(GIOChannel *source,
		  GIOCondition condition,
		  gpointer data) {
	return false;
}

static bool
init_led_config(struct led_config *config,
		GError **error) {
	int rc;
	bool device_already_opened = false;
	GList *values = g_hash_table_get_values(led_configs);
	int64_t *hash_key = g_new(int64_t, 1);

	if (!config->led_brightness_on)
		config->led_brightness_on = 1;

	config->led_path =
		g_realloc(config->led_path,
			  strlen(config->led_path) + sizeof("/brightness"));
	strcat(config->led_path, "/brightness");

	/* Check to make sure this config doesn't conflict with any of the
	 * others */
	for (GList *l = values; l != NULL; l = l->next) {
		struct led_config *c = l->data;

		if (strcmp(config->device_path, c->device_path) == 0) {
			if (!device_already_opened) {
				g_free(config->device_path);

				config->device_path = c->device_path;
				config->input_device = c->input_device;
				config->dev = c->dev;

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
			/* Strip the "/brightness" from the end of the string */
			*g_strrstr(config->led_path, "/brightness") = '\0';

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
		if (c->led_brightness_on == config->led_brightness_on) {
			config->led_brightness_on_str =
				c->led_brightness_on_str;
			config->led_brightness_on_strlen =
				c->led_brightness_on_strlen;
		}

		if (c->led_brightness_off == config->led_brightness_off) {
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
		GIOChannel *io_channel =
			g_io_channel_new_file(config->device_path, "r", error);

		g_return_val_if_fail(io_channel, false);

		g_return_val_if_fail(
		    g_io_channel_set_flags(io_channel, G_IO_FLAG_NONBLOCK,
					   error) == G_IO_STATUS_NORMAL,
		    false);

		rc = libevdev_new_from_fd(g_io_channel_unix_get_fd(io_channel),
					  &config->dev);
		if (rc < 0) {
			fprintf(stderr,
				"Failed to initialize libevdev for \"%s\": %s\n",
				config->name, strerror(rc));
			exit(1);
		}

		g_return_val_if_fail(
		    g_io_channel_set_encoding(
			io_channel, NULL, error) == G_IO_STATUS_NORMAL,
		    false);
		config->input_device = io_channel;
		g_io_add_watch(io_channel,
			       G_IO_IN | G_IO_PRI,
			       update_led, config->dev);
	}

	config->led_device = g_io_channel_new_file(config->led_path, "w",
						   error);
	g_return_val_if_fail(config->led_device, false);

	g_return_val_if_fail(
	    g_io_channel_set_encoding(
		config->led_device, NULL, error) == G_IO_STATUS_NORMAL,
	    false);
	g_io_channel_set_buffered(config->led_device, false);

	*hash_key = get_led_config_hash_key(
	    g_io_channel_unix_get_fd(config->input_device),
	    config->keyboard_led);
	g_hash_table_insert(led_configs, hash_key, config);

	g_list_free(values);

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

		config->name = strdup(groups[i]);

		keyboard_led_str = g_key_file_get_string(key_file, groups[i],
							 "KeyboardLed", error);
		g_return_val_if_fail(keyboard_led_str != NULL, false);

		config->keyboard_led = parse_keyboard_led(keyboard_led_str,
							  error);
		g_return_val_if_fail(error != NULL, false);

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
		g_clear_error(error);

		config->led_brightness_off =
			g_key_file_get_integer(key_file, groups[i],
					       "BrightnessOff", error);
		g_return_val_if_fail(
		    config->led_brightness_off ||
		    g_error_matches(*error, G_KEY_FILE_ERROR,
				    G_KEY_FILE_ERROR_KEY_NOT_FOUND),
		    false);
		g_clear_error(error);

		g_return_val_if_fail(init_led_config(config, error), false);

		g_free(keyboard_led_str);
	}

	g_key_file_free(key_file);
	g_strfreev(groups);

	return true;
}

#ifdef WITH_SYSV_STYLE_INIT
static gboolean
cleanup_pid_file(void *data) {
	char *pid_file_path = data;

	g_warn_if_fail(remove(pid_file_path) == 0);

	exit(0);
}
#endif /* WITH_SYSV_STYLE_INIT */

int
main(int argc, char *argv[]) {
	GError *error = NULL;
	GOptionContext *option_context;
	GMainLoop *main_loop;

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

#ifdef WITH_SYSV_STYLE_INIT
	if (pid_file_path) {
		char *pid_str;

		pid_str = g_strdup_printf("%d", getpid());
		if (!g_file_set_contents(pid_file_path, pid_str, -1, &error)) {
			fprintf(stderr, "Error writing PID file: %s\n",
				error->message);
			exit(1);
		}

		g_unix_signal_add(SIGTERM, cleanup_pid_file, pid_file_path);
		g_unix_signal_add(SIGINT, cleanup_pid_file, pid_file_path);
		g_unix_signal_add(SIGHUP, cleanup_pid_file, pid_file_path);

		g_free(pid_str);
	}
#endif /* WITH_SYSV_STYLE_INIT */

	g_option_context_free(option_context);

	led_configs = g_hash_table_new(g_int64_hash, g_int64_equal);

	if (!parse_conf_file(&error)) {
		fprintf(stderr, "Config file error: %s\n",
			error->message);
		exit(1);
	}

	/* Connect to systemd to update the LED state after standby/hibernate */
	{
		GDBusConnection *dbus_connection =
			g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

		if (!dbus_connection) {
			fprintf(stderr, "Couldn't connect to dbus: %s\n",
				error->message);
			exit(1);
		}

#ifdef WITH_SYSTEMD
		g_dbus_connection_signal_subscribe(dbus_connection,
						   NULL,
						   "org.freedesktop.login1.Manager",
						   "PrepareForSleep",
						   NULL,
						   NULL,
						   G_DBUS_SIGNAL_FLAGS_NONE,
						   prepare_for_sleep,
						   NULL, NULL);
#endif /* WITH_SYSTEMD */
	}

	/* Initialize all of the LEDs to match their keyboard LEDs state */
	init_led_states();

	main_loop = g_main_loop_new(NULL, false);
	g_main_loop_run(main_loop);

	return 0;
}
