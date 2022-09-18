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
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>

#include "jsh.h"

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

	if ((!path && !(path = getenv("HOME"))) || !*path)
		return -1;

	if (chdir(path) == 0)
		jsh_set_prompt();
	else
		fprintf(stderr, "Failed to chdir '%s': %s\n",
			path, strerror(errno));

	return 0;
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
	#define MAX_HIST_LINE	128

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
	printf("%s\n", _JSH_VERSION_);
	return 0;
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
 * Univeral callback entry of jailed commands except for cd & exit.
 */
int
cmd_entry(cmd_arg_t *cmd_arg, int do_flag)
{
	int	i;
	char	*name, *value;
	char	*alias = NULL;
	char	*ptr, cmd_str[800];
	int	len, left;

	bzero(cmd_str, sizeof(cmd_str));
	ptr = &cmd_str[0];
	left = sizeof(cmd_str) - 1;

	for_each_cmd_arg(cmd_arg, i, name, value) {
		/* Only try alias for first CMD arg */
		if (i == 0 && IS_ARG(name, CMD)) {
			alias = get_alias(value);
			if (alias && alias[0])
				value = alias;
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
		exec_system_cmd(cmd_str);
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
			return 0;
		} else
			return -1;
	}

	/* Set group/user specific env if "env <NAME>=<VALUE>" parsed ok */
	if (strcmp(argv[0], "env") == 0) {
		if (argc == 2 && argv[1][0] != '=' && strchr(argv[1], '=')) {
			snprintf(env_str, sizeof(env_str), "%s", argv[1]);
			putenv(strdup(env_str));
			syslog(LOG_INFO, "setenv: %s\n", env_str);
		}
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
	if (get_cmd_tree(argv[0], ALL_VIEW_MASK, DO_FLAG, &ct) != 1) {
		if (!get_man_desc(argv[0], desc, sizeof(desc))) {
			snprintf(desc, sizeof(desc), "System %s command", argv[0]);
		}
		/* The callback arg of first keyword is always "CMD" */
		create_cmd_arg(&ct, argv[0], desc, ARG(CMD), cmd_entry);
		if (!ct) {
			fprintf(stderr, "Failed to create command '%s'\n", argv[0]);
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
			add_cmd_key_arg(ct, argv[i], NULL, ARG(OPT));
		}
	}

	/* Register syntax */
	add_cmd_easily(ct, syntax, BASIC_VIEW, DO_FLAG);

	/* If there is PATH arg, register path specific completion helper */
	if (has_path)
		set_cmd_arg_helper(ct, ARG(PATH), path_helper);

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
	char	path[64], env_str[80];

	mylex_init();

	/* Internal exit & cd commands */
	create_cmd(&cmd_tree, "exit", "Exit jsh", cmd_exit);
	add_cmd_easily(cmd_tree, "exit", BASIC_VIEW, DO_FLAG);

	create_cmd(&cmd_tree, "cd", "Change directory", cmd_cd);
	add_cmd_var(cmd_tree, "PATH", "Directory", LEX_PATH, ARG(PATH));
	add_cmd_easily(cmd_tree, "cd [ PATH ]", BASIC_VIEW, DO_FLAG);
	set_cmd_arg_helper(cmd_tree, ARG(PATH), dir_helper);

	create_cmd(&cmd_tree, "history", "Browse history", cmd_history);
	add_cmd_easily(cmd_tree, "history", BASIC_VIEW, DO_FLAG);

	create_cmd(&cmd_tree, "version", "Display jsh version", cmd_version);
	add_cmd_easily(cmd_tree, "version", BASIC_VIEW, DO_FLAG);

	grp = getgrgid(getgid());
	passwd = getpwuid(getuid());

	/* Set HOME env */
	snprintf(env_str, sizeof(env_str), "HOME=%s", passwd->pw_dir);
	putenv(strdup(env_str));

	/* Install commands from /usr/local/etc/jsh.d/group.<group>.conf*/
	sprintf(path, JSH_CONF_DIR "/group.%s.conf", grp->gr_name);
	jsh_read_conf(path);

	/* Install commands from /usr/local/etc/jsh.d/user.<user>.conf*/
	sprintf(path, JSH_CONF_DIR "/user.%s.conf", passwd->pw_name);
	jsh_read_conf(path);

	return 0;
}

/*
 * Try to authorize the scp command transfered by "jsh -c"
 */
int
auth_scp_exec(char *arg)
{
	char	*ptr, *dir;
	char	abs_dir[PATH_MAX];
	char	prefix[PATH_MAX];
	char	buf[1200];

	if (!arg || !arg) return -1;

	/* Must have "env SCPEXEC=1" or "env SCPEXEC=True" */
	if (!(ptr = getenv("SCPEXEC")) ||
	    (strcmp(ptr, "1") != 0 && strcasecmp(ptr, "True") != 0)) {
		syslog(LOG_WARNING, "Unautorized -c '%s'", arg);
		goto err_out;
	}

	/* Only "scp -f" or "scp -t" is acceptible */
	if (strncmp(arg, "scp -f ", 7) != 0 &&
	    strncmp(arg, "scp -t ", 7) != 0) {
		syslog(LOG_WARNING, "Not scp: '%s'", arg);
		goto err_out;
	}
	if (!*(ptr = arg + 7)) {
		syslog(LOG_WARNING, "Empty scp directory, abort");
		goto err_out;
	}

	if (!get_abs_dir(ptr, abs_dir, sizeof(abs_dir)))
		goto err_out;

	if (!(dir = getenv("HOME")) || !*dir)
		goto err_out;

#ifdef DEBUG_JSH
	syslog(LOG_DEBUG, "abs_dir='%s' HOME='%s'", abs_dir[0] ? abs_dir:"", dir);
#endif

	/* Try HOME dir prefix match */
	if (dir[strlen(dir) - 1] != '/')
		snprintf(prefix, sizeof(prefix), "%s/", dir);
	if (strncmp(abs_dir, prefix, strlen(prefix)) == 0) {
		syslog(LOG_INFO, "'%s' matches home '%s'\n",
		       abs_dir[0] ? abs_dir:"", prefix);
		return 0;
	}

	/* Try each prefix match of SCPDIR */
	if (!(dir = getenv("SCPDIR")))
		goto err_out;

	snprintf(buf, sizeof(buf), "%s", dir);
	dir = strtok(buf, ":");
	while (dir) {
		if (!*dir) continue;
		if (dir[strlen(dir) - 1] != '/')
			snprintf(prefix, sizeof(prefix), "%s/", dir);
		if (strncmp(abs_dir, prefix, strlen(prefix)) == 0) {
			syslog(LOG_INFO, "'%s' matches scpdir '%s'\n",
			       abs_dir[0] ? abs_dir:"", prefix);
			return 0;
		}
		dir = strtok(NULL, ":");
	}

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

	/* Default aliases to disable vi/vim executing external commands */
	putenv("alias_vi=vim -Z");
	putenv("alias_vim=vim -Z");

	ocli_rl_init();
	cmd_manual_init();

	jsh_init();

	/* Try jailed scp */
	if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
#ifdef DEBUG_JSH
		for (int i = 1; i < argc; i++)
			syslog(LOG_DEBUG, "arg[%d] = '%s'", i, argv[i]);
#endif
		if ((res = auth_scp_exec(argv[2])) == 0)
			res = exec_system_cmd(argv[2]);
		goto out;
	}

	ocli_rl_set_eof_cmd("exit");
	ocli_rl_set_timeout(300);
	ocli_rl_set_view(BASIC_VIEW);
	jsh_set_prompt(BASIC_VIEW);

	ocli_rl_loop();
out:
	ocli_rl_exit();
	return res;
}
