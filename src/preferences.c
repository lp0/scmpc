/**
 * preferences.c: Preferences parsing
 *
 * ==================================================================
 * Copyright (c) 2009-2013 Christoph Mende <mende.christoph@gmail.com>
 * Based on Jonathan Coome's work on scmpc
 *
 * This file is part of scmpc.
 *
 * scmpc is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * scmpc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with scmpc; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 * ==================================================================
 */


#include <stdlib.h>
#include <string.h>

#include <confuse.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "scmpc.h"
#include "preferences.h"

static gint cf_log_level(cfg_t *cfg, cfg_opt_t *opt, const gchar *value,
		void *result);
static gint cf_validate_num(cfg_t *cfg, cfg_opt_t *opt);
static void free_config_files(gchar **config_files);
static gboolean parse_files(cfg_t *cfg);
static gchar* expand_tilde(const gchar *path);
static gboolean parse_config_file(void);
static gboolean parse_command_line(gint argc, gchar **argv);

/**
 * Parse log level values from the config file and set result to a
 * GLogLevelFlags value
 */
static gint cf_log_level(cfg_t *cfg, cfg_opt_t *opt, const gchar *value,
		void *result)
{
	if (!strncmp(value, "none", 4))
		*(GLogLevelFlags *)result = G_LOG_LEVEL_ERROR;
	else if (!strncmp(value, "error", 5))
		*(GLogLevelFlags *)result = G_LOG_LEVEL_ERROR;
	else if (!strncmp(value, "warning", 7))
		*(GLogLevelFlags *)result = G_LOG_LEVEL_WARNING;
	else if (!strncmp(value, "info", 4))
		*(GLogLevelFlags *)result = G_LOG_LEVEL_MESSAGE;
	else if (!strncmp(value, "debug", 5))
		*(GLogLevelFlags *)result = G_LOG_LEVEL_DEBUG;
	else {
		cfg_error(cfg, "Invalid value for option '%s': '%s'",
			cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

/**
 * Check if the given opt value is non-negative
 */
static gint cf_validate_num(cfg_t *cfg, cfg_opt_t *opt)
{
	gint value = cfg_opt_getnint(opt, 0);
	if (value < 0) {
		cfg_error(cfg, "'%s' in section '%s' must be a non-negative value",
			cfg_opt_name(opt), cfg_name(cfg));
		return -1;
	}
	return 0;
}

/**
 * Release resources
 */
static void free_config_files(gchar **config_files)
{
	for (int i = 0; i < 3; i++)
		g_free(config_files[i]);
}

/**
 * Search for a working config file and parse it
 */
static gboolean parse_files(cfg_t *cfg)
{
	gchar *config_files[3];
	const gchar *home;

	if (!prefs.config_file) {
		if (!(home = g_getenv("HOME")))
			home = g_get_home_dir();

		config_files[0] = g_strdup_printf("%s/.scmpcrc", home);
		config_files[1] = g_strdup_printf("%s/.scmpc/scmpc.conf", home);
		config_files[2] = g_strdup(SYSCONFDIR "/scmpc.conf");
	} else {
		config_files[0] = prefs.config_file;
		config_files[1] = g_strdup("");
		config_files[2] = g_strdup("");
	}

	for (gint i = 0; i < 3; i++)
	{
		switch(cfg_parse(cfg, config_files[i]))
		{
			case CFG_PARSE_ERROR:
				fprintf(stderr, "%s: This configuration file "
				"contains errors and cannot be parsed.\n",
				config_files[i]);
				free_config_files(config_files);
				return FALSE;
			case CFG_FILE_ERROR:
				break;
			case CFG_SUCCESS:
				free_config_files(config_files);
				return TRUE;
			default:
				free_config_files(config_files);
				return FALSE;
		}
	}
	fprintf(stderr, "Couldn't find any valid configuration files.\n");
	return FALSE;
}

/**
 * Expand ~ to the user's home dir
 */
static gchar* expand_tilde(const gchar *path)
{
	if (path[0] == '~') {
		const gchar *home = getenv("HOME");
		if (!home)
			home = g_get_home_dir();

		return g_strconcat(home, &path[1], NULL);
	}

	return g_strdup(path);
}

/**
 * Parse config file options
 */
static gboolean parse_config_file(void)
{
	cfg_t *cfg, *sec_as, *sec_mpd;

	cfg_opt_t mpd_opts[] = {
		CFG_STR("host", "localhost", CFGF_NONE),
		CFG_INT("port", 6600, CFGF_NONE),
		CFG_INT("timeout", 5, CFGF_NONE),
		CFG_INT("interval", 10, CFGF_NONE),
		CFG_STR("password", "", CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t as_opts[] = {
		CFG_STR("username", "", CFGF_NONE),
		CFG_STR("password", "", CFGF_NONE),
		CFG_STR("password_hash", "", CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t opts[] = {
		CFG_INT_CB("log_level", G_LOG_LEVEL_ERROR, CFGF_NONE,
				&cf_log_level),
		CFG_STR("log_file", "/var/log/scmpc.log", CFGF_NONE),
		CFG_STR("pid_file", "/var/run/scmpc.pid", CFGF_NONE),
		CFG_STR("cache_file", "/var/lib/scmpc/scmpc.cache", CFGF_NONE),
		CFG_INT("queue_length", 500, CFGF_NONE),
		CFG_INT("cache_interval", 10, CFGF_NONE),
		CFG_SEC("mpd", mpd_opts, CFGF_NONE),
		CFG_SEC("audioscrobbler", as_opts, CFGF_NONE),
		CFG_END()
	};

	cfg = cfg_init(opts, CFGF_NONE);
	cfg_set_validate_func(cfg, "queue_length", &cf_validate_num);
	cfg_set_validate_func(cfg, "cache_interval", &cf_validate_num);
	cfg_set_validate_func(cfg, "mpd|port", &cf_validate_num);
	cfg_set_validate_func(cfg, "mpd|timeout", &cf_validate_num);

	if (parse_files(cfg) == FALSE) {
		cfg_free(cfg);
		return FALSE;
	}

	g_free(prefs.log_file);
	g_free(prefs.pid_file);
	g_free(prefs.cache_file);
	g_free(prefs.mpd_hostname);
	g_free(prefs.mpd_password);
	g_free(prefs.as_username);
	g_free(prefs.as_password);
	g_free(prefs.as_password_hash);

	prefs.log_level = cfg_getint(cfg, "log_level");
	prefs.log_file = expand_tilde(cfg_getstr(cfg, "log_file"));
	prefs.pid_file = expand_tilde(cfg_getstr(cfg, "pid_file"));
	prefs.cache_file = expand_tilde(cfg_getstr(cfg, "cache_file"));
	prefs.queue_length = cfg_getint(cfg, "queue_length");
	prefs.cache_interval = cfg_getint(cfg, "cache_interval");

	sec_mpd = cfg_getsec(cfg, "mpd");
	prefs.mpd_hostname = g_strdup(cfg_getstr(sec_mpd, "host"));
	prefs.mpd_port = cfg_getint(sec_mpd, "port");
	prefs.mpd_timeout = cfg_getint(sec_mpd, "timeout");
	prefs.mpd_password = g_strdup(cfg_getstr(sec_mpd, "password"));

	sec_as = cfg_getsec(cfg, "audioscrobbler");
	prefs.as_username = g_strdup(cfg_getstr(sec_as, "username"));
	prefs.as_password = g_strdup(cfg_getstr(sec_as, "password"));
	prefs.as_password_hash = g_strdup(cfg_getstr(sec_as, "password_hash"));

	prefs.fork = TRUE;

	cfg_free(cfg);
	return TRUE;
}

/**
 * Parse command line options
 */
static gboolean parse_command_line(gint argc, gchar **argv)
{
	GError *error = NULL;
	gchar *pid_file = NULL, *conf_file = NULL;
	gboolean dokill = FALSE, debug = FALSE, quiet = FALSE, version = FALSE;
	gboolean fork = TRUE;
	GOptionEntry entries[] = {
		{ "debug", 'd', 0, G_OPTION_ARG_NONE, &debug,
			"Log everything.", NULL },
		{ "kill", 'k', 0, G_OPTION_ARG_NONE, &dokill
			, "Kill the running scmpc", NULL },
		{ "quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet,
			"Disable logging.", NULL },
		{ "config-file", 'f', 0, G_OPTION_ARG_FILENAME, &conf_file,
			"The location of the configuration file.",
			"<config_file>" },
		{ "pid-file", 'i', 0, G_OPTION_ARG_FILENAME, &pid_file,
			"The location of the pid file.", "<pid_file>" },
		{ "version", 'v', 0, G_OPTION_ARG_NONE, &version,
			"Print the program version.", NULL },
		{ "foreground", 'n', G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE,
			&fork, "Run the program in the foreground rather "
				"than as a daemon.", NULL },
		{ NULL, 0, 0, 0, NULL, NULL, NULL }
	};

	GOptionContext *context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_print("%s\n", error->message);
		g_option_context_free(context);
		return FALSE;
	}
	g_option_context_free(context);

	if (version) {
		puts(PACKAGE_STRING);
		puts("An Audioscrobbler client for MPD.");
		puts("Copyright 2009-2013 Christoph Mende "
				"<mende.christoph@gmail.com>");
		puts("Based on Jonathan Coome's work on scmpc");
		exit(EXIT_SUCCESS);
	}
	/* This must be at the top, to avoid any options specified in the
	 * config file overriding those on the command line. */
	if (conf_file) {
		g_free(prefs.config_file);
		prefs.config_file = g_strdup(conf_file);
	} else {
		prefs.config_file = NULL;
	}
	if (!parse_config_file())
		return FALSE;

	if (pid_file) {
		g_free(prefs.pid_file);
		prefs.pid_file = g_strdup(pid_file);
	}
	if (quiet && debug) {
		fputs("Specifying --debug and --quiet at the same time does "
				"not make any sense.", stderr);
		return FALSE;
	} else if (quiet) {
		prefs.log_level = G_LOG_LEVEL_ERROR;
	} else if (debug) {
		prefs.log_level = G_LOG_LEVEL_DEBUG;
	}
	if (!fork)
		prefs.fork = FALSE;
	if (dokill)
		kill_scmpc();
	g_free(pid_file);
	g_free(conf_file);
	return TRUE;
}

gboolean init_preferences(gint argc, gchar **argv)
{
	gchar *tmp, *saveptr;

	if (parse_command_line(argc, argv) == FALSE)
		return FALSE;

	tmp = getenv("MPD_HOST");
	if (tmp) {
		g_free(prefs.mpd_password);
		g_free(prefs.mpd_hostname);
		prefs.mpd_password = g_strdup("");
		prefs.mpd_hostname = g_strdup(tmp);
	}
	if (getenv("MPD_PORT"))
		prefs.mpd_port = strtol(getenv("MPD_PORT"), NULL, 10);

	return TRUE;
}

void clear_preferences(void)
{
	g_free(prefs.mpd_hostname);
	g_free(prefs.mpd_password);
	g_free(prefs.config_file);
	g_free(prefs.log_file);
	g_free(prefs.pid_file);
	g_free(prefs.cache_file);
	g_free(prefs.as_username);
	g_free(prefs.as_password);
	g_free(prefs.as_password_hash);
}
