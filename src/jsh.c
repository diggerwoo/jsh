/*
 * jsh, a simple jailed shell based on libocli.
 *
 * Copyright (C) 2022 Digger Wu (digger.wu@linkbroad.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>

#include "jsh.h"

char	*home_dir = NULL;
char	*sftp_server = SFTP_SERVER;

/* Enable home jailed by default */
int	home_jailed = 1;

/*
 * Set jsh specific readline prompt
 */
void
jsh_set_prompt()
{
	char	cwd[256];
	char	*base = NULL;
	char	host[32];
	char	prompt[64];

	if (getcwd(cwd, sizeof(cwd)))
		base = basename(cwd);

	bzero(host, sizeof(host));
	gethostname(host, sizeof(host)-1);
	snprintf(prompt, sizeof(prompt), "[%s %s]> ",
		host, base ? base:"");
	ocli_rl_set_prompt(prompt);
}

/*
 * Callback of exit command
 */
static int
cmd_exit(cmd_arg_t *cmd_arg, int do_flag)
{
	ocli_rl_finished = 1;
	return 0;
}

/*
 * Callback of cd command
 */
static int
cmd_cd(cmd_arg_t *cmd_arg, int do_flag)
{
	int	i;
	char	*name, *value;
	char	*path = NULL;

	for_each_cmd_arg(cmd_arg, i, name, value) {
		if (IS_ARG(name, PATH))
			path = value;
	}

	if (!path || !path[0]) {
		path = home_dir;
	} else if (home_jailed && !in_home_dirs(path)) {
		fprintf(stderr, "Access denied to '%s'\n",
			value);
		return -1;
	}

	if (chdir(path) == 0)
		jsh_set_prompt();
	else
		fprintf(stderr, "Failed to chdir '%s': %s\n",
			path, strerror(errno));

	return 0;
}

/*
 * History utilities
 */
#define MAX_HIST_LINE	128
#define JSH_HIST_FILE	".jsh_history"

static int
save_history()
{
	char	path[128];

	snprintf(path, sizeof(path), "%s/%s", home_dir, JSH_HIST_FILE);
	stifle_history(MAX_HIST_LINE);
	return write_history(path);
}

static int
load_history()
{
	char	path[128];

	snprintf(path, sizeof(path), "%s/%s", home_dir, JSH_HIST_FILE);
	return read_history(path);
}

/*
 * Callback of history command
 */
static int
cmd_history(cmd_arg_t *cmd_arg, int do_flag)
{
	HIST_ENTRY **his_list;
	int	i, n, len, left;
	char	*ptr, *buf;

	stifle_history(MAX_HIST_LINE);
	if (!(his_list = history_list())) return 0;

	len = MAX_HIST_LINE * 256;
	if (!(buf = malloc(len))) {
		fprintf(stderr, "history malloc: %s\n", strerror(errno));
		return -1;
	}
	bzero(buf, len);
	left = len;
	ptr = buf;
	for (i = 0; his_list[i]; i++) {
		if (left - strlen(his_list[i]->line) - 8 < 0) break;
		ptr += (n = snprintf(ptr, left, "%5d  %s\n", i + 1, his_list[i]->line));
		left -= n;
	}

	display_buf_more(buf, ptr - buf);
	free(buf);
	return 0;
}

/*
 * Callback of version command
 */
static int
cmd_version(cmd_arg_t *cmd_arg, int do_flag)
{
	return printf("%s\n", JSH_VERSION);
}

/*
 * Get alias command from env
 */
char *
get_alias(char *cmd)
{
	char	name[64];
	snprintf(name, sizeof(name), "alias_%s", cmd);
	return getenv(name);
}

/*
 * Universal callback entry of jailed commands except for cd & exit.
 */
int
cmd_entry(cmd_arg_t *cmd_arg, int do_flag)
{
	int	i;
	char	*name, *value;
	char	*alias = NULL;
	char	*ptr, cmd_str[800];
	int	len, left;
	int	wildcard = 0, pipe_rdr = 0;
	int	jailed_exec = 0;
	struct stat stat_buf;

	bzero(cmd_str, sizeof(cmd_str));
	ptr = &cmd_str[0];
	left = sizeof(cmd_str) - 1;

	for_each_cmd_arg(cmd_arg, i, name, value) {
		/* Only try alias for first CMD arg */
		if (i == 0 && IS_ARG(name, CMD)) {
			/* Override jailed exec for vim */
			if (home_jailed &&
			    (strcmp(value, "vi") == 0 ||
			     strcmp(value, "vim") == 0))
				jailed_exec = 1;

			alias = get_alias(value);
			if (alias && alias[0])
				value = alias;
		}

		/* Wildcard *, pipline |, or rdr > >> present */
		if (IS_ARG(name, PATH)) {
			if (home_jailed && !in_home_dirs(value)) {
				fprintf(stderr, "Access denied to '%s'\n",
					value);
				return -1;
			}
			if (strchr(value, '*') &&
			    stat(value, &stat_buf) < 0 && errno == ENOENT)
				wildcard++;
		} else if (IS_ARG(name, OPT)) {
			if (strcmp(value, "|") == 0 ||
			    strcmp(value, ">") == 0 ||
			    strcmp(value, ">>") == 0)
				pipe_rdr++;
		}

		if (strlen(value) < left - 3) {
			if (i != 0 && strchr(value, ' '))
				ptr += (len = snprintf(ptr, left, "\"%s\" ", value));
			else
				ptr += (len = snprintf(ptr, left, "%s ", value));
			left -= len;
		} else {
			fprintf(stderr, "Line buffer full, abort EXEC\n");
			return -1;
		}
	}

	if (cmd_str[0]) {
		if (jailed_exec) 
			exec_system_cmd(cmd_str, JAILED_EXEC);
		else if (wildcard > 0 || pipe_rdr > 0) 
			exec_system_cmd(cmd_str, SH_CMD_EXEC);
		else
			exec_system_cmd(cmd_str, COMMON_EXEC);
	}

	return 0;
}

/*
 * Register a jsh command syntax
 */
int
reg_jsh_cmd(char *syntax)
{
	char	**argv = NULL;
	int	argc = 0;
	int	i, lex_type;
	int	arg_type[MAX_ARG_NUM];
	char	desc[80];
	char	*ptr = NULL, env_str[256];
	int	has_path = 0;
	struct cmd_tree *ct = NULL;

	bzero(arg_type, sizeof(arg_type));
	if ((argc = get_argv(syntax, &argv, NULL)) <= 0)
		return -1;

	/* Install an alias if "alias <cmd> <alias_cmd>" parsed ok */
	if (strcmp(argv[0], "alias") == 0) {
		if (argc == 3 &&
		    !is_captical_word(argv[1]) &&
		    (ptr = strtok(argv[2], "\""))) {
			snprintf(env_str, sizeof(env_str), "alias_%s=%s", argv[1], ptr);
			putenv(strdup(env_str));
			syslog(LOG_INFO, "setenv: %s\n", env_str);
			if (argv) free_argv(argv);
			return 0;
		} else {
			if (argv) free_argv(argv);
			return -1;
		}
	}

	/* Set group/user specific env if "env <NAME>=<VALUE>" parsed ok */
	if (strcmp(argv[0], "env") == 0) {
		if (argc == 2 && argv[1][0] != '=' && strchr(argv[1], '=')) {
			snprintf(env_str, sizeof(env_str), "%s", argv[1]);
			putenv(strdup(env_str));
			syslog(LOG_INFO, "setenv: %s\n", env_str);
		}
		if (argv) free_argv(argv);
		return 0;
	}

	/* Captial word should try matching internal lexical name first */
	for (i = 0; i < argc; i++) {
		if (is_captical_word(argv[i]) &&
		    (lex_type = get_lex_type(argv[i])) >= 0)
			arg_type[i] = lex_type;
		else
			arg_type[i] = -1;
	}

	/* Create a syntax tree if it is a new command */
	if (!(ct = get_cmd_tree(argv[0]))) {
		if (!get_man_desc(argv[0], desc, sizeof(desc))) {
			snprintf(desc, sizeof(desc), "System %s command", argv[0]);
		}
		/* The callback arg of first keyword is always "CMD" */
		create_cmd_arg(&ct, argv[0], desc, ARG(CMD), cmd_entry);
		if (!ct) {
			fprintf(stderr, "Failed to create command '%s'\n", argv[0]);
			if (argv) free_argv(argv);
			return -1;
		}
	}

	/* Add each key or var symbol */
	for (i = 1; i < argc; i++) {
		if (arg_type[i] >= 0) {
			if (arg_type[i] == LEX_PATH) {
				add_cmd_var(ct, argv[i], NULL, arg_type[i], ARG(PATH));
				has_path++;
			} else
				add_cmd_var(ct, argv[i], NULL, arg_type[i], argv[i]);
		} else {
			add_cmd_key_arg(ct, argv[i],
					get_man_opt_desc(argv[0], argv[i], desc, sizeof(desc)) ?
						desc : NULL,
					ARG(OPT));
		}
	}

	/* Register syntax */
	add_cmd_easily(ct, syntax, BASIC_VIEW, DO_FLAG);

	/* If there is PATH arg, register path specific completion helper */
	if (has_path)
		set_cmd_arg_helper(ct, ARG(PATH), path_helper);

	if (argv) free_argv(argv);
	return 0;
}

/*
 * Read syntax and install commands
 */
int
jsh_read_conf(char *path)
{
	FILE	*fp;
	char	buf[512];
	char	*ptr = NULL, *line = NULL;

	if ((fp = fopen(path, "r")) == NULL)
		return -1;

	while (fgets(buf, sizeof(buf), fp)) {
		ptr = buf;
		while (*ptr && isspace(*ptr)) ptr++;
		/* The first char should be a lowercase letter */
		if (!*ptr || !islower(*ptr)) continue;
		if (!(line = strtok(ptr, "\r\n"))) continue;
		reg_jsh_cmd(line);
	}

	fclose(fp);
	return 0;
}

/*
 * Init jsh commands
 */
int
jsh_init()
{
	struct cmd_tree *cmd_tree;
	struct passwd	*passwd = NULL;
	struct group	*grp = NULL;
	struct stat	stat_buf;
	char	*ptr, path[64], env_str[80];

	jsh_man_init();
	mylex_init();

	/* Builtin exit/quit & cd commands */
	create_cmd(&cmd_tree, "exit", "Exit jsh", cmd_exit);
	add_cmd_easily(cmd_tree, "exit", BASIC_VIEW, DO_FLAG);
	create_cmd(&cmd_tree, "quit", "Exit jsh", cmd_exit);
	add_cmd_easily(cmd_tree, "quit", BASIC_VIEW, DO_FLAG);

	create_cmd(&cmd_tree, "cd", "Change directory", cmd_cd);
	add_cmd_var(cmd_tree, "PATH", "Directory", LEX_PATH, ARG(PATH));
	add_cmd_easily(cmd_tree, "cd [ PATH ]", BASIC_VIEW, DO_FLAG);
	set_cmd_arg_helper(cmd_tree, ARG(PATH), dir_helper);

	/* Builtin history & version commands */
	create_cmd(&cmd_tree, "history", "Browse history", cmd_history);
	add_cmd_easily(cmd_tree, "history", BASIC_VIEW, DO_FLAG);

	create_cmd(&cmd_tree, "version", "Display jsh version", cmd_version);
	add_cmd_easily(cmd_tree, "version", BASIC_VIEW, DO_FLAG);

	if (!(grp = getgrgid(getgid()))) {
		perror("getgrgid");
		exit(-1);
	}
	if (!(passwd = getpwuid(getuid()))) {
		perror("getpwuid");
		exit(-1);
	}

	if (!passwd->pw_dir || !passwd->pw_dir[0]) {
		fprintf(stderr, "Weird missing home\n");
		exit(-1);
	}

	home_dir = strdup(passwd->pw_dir);

	/* Default disable per user .jsh_debug */
	snprintf(path, sizeof(path), "%s/.jsh_debug", home_dir);
	if (stat(path, &stat_buf) != 0)
		setlogmask(~LOG_MASK(LOG_DEBUG));

	/* Install commands from /usr/local/etc/jsh.d/group.<group>.conf*/
	snprintf(path, sizeof(path), JSH_CONF_DIR "/group.%s.conf", grp->gr_name);
	jsh_read_conf(path);

	/* Install commands from /usr/local/etc/jsh.d/user.<user>.conf*/
	snprintf(path, sizeof(path), JSH_CONF_DIR "/user.%s.conf", passwd->pw_name);
	jsh_read_conf(path);

	snprintf(env_str, sizeof(env_str), "HOME=%s", home_dir);
	putenv(strdup(env_str));

	if ((ptr = getenv("HOMEJAIL")) &&
	    (strcmp(ptr, "0") == 0 || strcasecmp(ptr, "False") == 0)) {
		home_jailed = 0;
		syslog(LOG_WARNING, "HOMEJAIL is disabled");
	} else 
		syslog(LOG_INFO, "HOMEJAIL enabled on %s", home_dir);

	/* Avoid crontab -e executing external commands */
	putenv("VISUAL=vim -Z");
	putenv("EDITOR=vim -Z");

	/* Default aliases to disable vi/vim executing external commands */
	putenv("alias_vi=vim -Z");
	putenv("alias_vim=vim -Z");

	chdir(home_dir);
	load_history();
	return 0;
}

/*
 * Cleanup jsh resources
 */
void
jsh_exit()
{
	ocli_rl_exit();
	jsh_man_exit();
}

/*
 * Has "env SCPEXEC=1" or "env SCPEXEC=True"
 */
static int
scp_enabled()
{
	char	*ptr; 
	return  ((ptr = getenv("SCPEXEC")) &&
		 (strcmp(ptr, "1") == 0 || strcasecmp(ptr, "True") == 0));
}

/*
 * Get sftp-server path
 */
static char *
get_sftp_server()
{
	char	*path, *base; 
	struct stat stat_buf;
	if ((path = getenv("SFTP_SERVER"))) {
		base = get_basename(path);
		if (base && strcmp(base, "sftp-server") == 0 &&
		    stat(path, &stat_buf) == 0 &&
		    S_ISREG(stat_buf.st_mode))
			return path;
		else
			return NULL;
	} else
		return SFTP_SERVER;
}

/*
 * Try to authorize the scp command transfered by "jsh -c"
 */
int
auth_scp_exec(char *arg)
{
	char	*ptr;

	if (!arg || !arg) return -1;

	/* Only "scp -f" or "scp -t" is acceptible */
	if (strncmp(arg, "scp -f ", 7) != 0 &&
	    strncmp(arg, "scp -t ", 7) != 0) {
		syslog(LOG_WARNING, "Invalid scp: '%s'", arg);
		goto err_out;
	}
	if (!*(ptr = arg + 7)) {
		syslog(LOG_WARNING, "Empty scp directory, abort");
		goto err_out;
	}

	if (in_home_dirs(ptr)) return 0;
err_out:
	printf("Unauthorized access, abort\n");
	return -1;
}

/*
 * main entry, -c is limited to jailed scp
 */
int
main(int argc, char **argv)
{
	int	res = 0;

	openlog("jsh", LOG_PID, LOG_AUTHPRIV);

	/* Default PATH env to find all executables */
	putenv("PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin");

	ocli_rl_init();
	cmd_manual_init();

	jsh_init();

	/* Try jailed scp */
	if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
		if (!scp_enabled()) goto out;
		syslog(LOG_DEBUG, "-c '%s'", argv[2]);

		if ((sftp_server = get_sftp_server()) &&
		    strcmp(argv[2], sftp_server) == 0)
			res = exec_system_cmd(argv[2], JAILED_EXEC);
		else if ((res = auth_scp_exec(argv[2])) == 0)
			res = exec_system_cmd(argv[2], COMMON_EXEC);
		goto out;
	}

	ocli_rl_set_eof_cmd("exit");
	ocli_rl_set_timeout(300);
	ocli_rl_set_view(BASIC_VIEW);
	jsh_set_prompt(BASIC_VIEW);

	ocli_rl_loop();
	save_history();
out:
	jsh_exit();
	return res;
}
