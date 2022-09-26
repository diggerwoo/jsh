/*
 * jsh, a simple jailed shell based on libocli.
 *
 * Copyright (C) 2022-2022 Digger Wu (digger.wu@linkbroad.com)
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
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <libgen.h>
#include <syslog.h>
#include <ocli/ocli.h>
#include <sys/ptrace.h>

#include "jsh.h"
#include "man.h"

/*
 * Exec external commands
 */
int
exec_system_cmd(char *cmd, int mode)
{
	pid_t	pid;
	int	argc = 0;
	char	**argv = NULL;
	int	status, res = 0;

	if (!cmd || !cmd[0]) return -1;
	syslog(LOG_DEBUG, "exec [%s] mode [%d]", cmd, mode);

	if (mode != SH_CMD_EXEC) {
		if ((argc = get_argv(cmd, &argv, NULL)) <= 0) {
			fprintf(stderr, "exec_system_cmd: bad args");
			return -1;
		}
	}

	if ((pid = fork()) == 0) {
		ocli_rl_exit();

		if (mode == SH_CMD_EXEC) {
			res = execlp("sh", "sh", "-c", cmd, NULL);
		} else {
			if (mode == JAILED_EXEC)
				ptrace(PTRACE_TRACEME, 0, NULL, NULL);
			res = execvp(argv[0], argv);
		}

		if (res < 0) {
			fprintf(stderr, "exec_system_cmd execvp: %s",
				strerror(errno));
			if (argv) free_argv(argv);
                        exit(-1);
		}

	} else if (pid != -1) {
		if (mode == JAILED_EXEC) {
			if (jtrace(pid, argc, argv) == 0)
				goto out;
		}
		if ((res = waitpid(pid, &status, 0)) < 0)
			syslog(LOG_ERR, "waitpid %d [%s] = %d: %s",
				pid, cmd, res, strerror(errno));
		/* Better zero output and return 0 on success for scp */
		if (argv && argv[0] && strcmp(argv[0], "scp") == 0) {
			if (res >= 0) res = 0;
			goto out;
		}
		/* New line if child process terminated by signals */
		if (WIFSIGNALED(status))
			printf("\n");
	} else {
		fprintf(stderr, "exec_system_cmd fork: %s",
			strerror(errno));
		res = -1;
	}

out:
	if (argv) free_argv(argv);
	return res;
}

/*
 * Get man description line from auto generated man[] array in man.h
 */
char *
get_man_desc(char *cmd, char *desc, int len)
{
	struct man *ptr;
	int	n = 0;

	if (!cmd || !cmd[0] || !desc || len < 8) return NULL;
	if ((n = (sizeof(man) / sizeof(struct man))) < 2) return NULL;

	bzero(desc, len);
	ptr = &man[0];

	while (ptr->cmd && ptr->cmd[0] && n > 0) {
		if (strcmp(cmd, ptr->cmd) == 0) {
			if (ptr->desc && ptr->desc[0]) {
				snprintf(desc, len, "%s", ptr->desc);
				desc[0] = toupper(desc[0]);
			}
			break;
		}
		ptr++; n--;
	}
	return ((desc[0]) ? desc : NULL);
}

/*
 * Where is GNU version basename() ?
 */
char *
get_basename(char *path)
{
	char	*base = NULL;
	if (!path || !path[0]) return NULL;

	base = path + strlen(path) - 1;
	if (*base == '/')
		return NULL;

	while (base > path) {
		if (*(base-1) == '/')
			break;
		else
			base--;
	}
	return base;
}

/*
 * Get absolute directory of the path, ugly by chdir & getcwd.
 * Set full 1 for getting full path including the file name.
 */
char *
get_abs_dir(char *path, char *abs_dir, int len, int full)
{
	char	*dir, *fname = NULL;
	char	buf[PATH_MAX];
	char	cwd[PATH_MAX];
	int	n;
	struct stat stat_buf;

	if (!path || !path[0] || !abs_dir || len < 8)
		return NULL;

	snprintf(buf, sizeof(buf), "%s", path);
	if (stat(buf, &stat_buf) == 0 && S_ISDIR(stat_buf.st_mode)) {
		dir = buf;
	} else {
		if (!(dir = dirname(buf)))
			return NULL;
		if (full)
			fname = get_basename(path);
	}

	if (!getcwd(cwd, sizeof(cwd))) return NULL;

	if (chdir(dir) != 0) {
		syslog(LOG_ERR, "get_abs_dir chdir '%s': %s",
		       dir, strerror(errno));
		return NULL;
	}

	if (!getcwd(abs_dir, len)) return NULL;

	/* getcwd does not append tailing '/', but we need trailing '/'
	 * for acurate dir prefix match
	 */
	n = strlen(abs_dir);
	if (abs_dir[n - 1] != '/') {
		if (n < len - 1) {
			strncat(abs_dir, "/", 1);
		} else {
			syslog(LOG_ERR, "get_abs_dir: no space to append '/' to '%s'",
			       abs_dir);
			chdir(cwd);
			return NULL;
		}
	}

	if (full && fname && *fname) {
		n = strlen(abs_dir) + strlen(fname);
		if (n < len) {
			strcat(abs_dir, fname);
		} else {
			syslog(LOG_ERR, "get_abs_dir full: no space to append '%s' to '%s'",
			       fname, abs_dir);
			chdir(cwd);
			return NULL;
		}
	}

	chdir(cwd);
	return abs_dir;
}

/*
 * Test if dir or file entry is in subdir of path.
 * The path may contain multi entires separatd by colon.
 */ 
int
in_subdir(char *ent, char *path)
{
	char	*dir;
	char	abs_dir[PATH_MAX];
	char	prefix[PATH_MAX];
	char	buf[512];

	if (!ent || !*ent || !path || !*path)
		return 0;

	if (!get_abs_dir(ent, abs_dir, sizeof(abs_dir), 0))
		return 0;

	snprintf(buf, sizeof(buf), "%s", path);
	dir = strtok(buf, ":");
	while (dir) {
		if (!*dir) continue;
		if (dir[strlen(dir) - 1] != '/')
			snprintf(prefix, sizeof(prefix), "%s/", dir);
		else
			snprintf(prefix, sizeof(prefix), "%s", dir);
		if (strncmp(abs_dir, prefix, strlen(prefix)) == 0) {
			syslog(LOG_INFO, "'%s' is subdir of '%s'\n",
			       abs_dir, prefix);
			return 1;
		}
		dir = strtok(NULL, ":");
	}
	syslog(LOG_DEBUG, "'%s' is not any subdir of '%s'\n",
	       abs_dir, path);
	return 0;
}
