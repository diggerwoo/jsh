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

/*
 * exec external commands
 */
int
exec_system_cmd(char *cmd)
{
	pid_t	pid;
	int	argc = 0;
	char	**argv = NULL;
	int	res = 0;

	if (!cmd || !cmd[0]) return -1;
#ifdef DEBUG_JSH
	syslog(LOG_DEBUG, "exec [%s]\n", cmd);
#endif

	if ((pid = fork()) == 0) {
		argc = get_argv(cmd, &argv, NULL);
		if (execvp(argv[0], argv) < 0) {
			fprintf(stderr, "exec_system_cmd execvp: %s",
				strerror(errno));
			free_argv(argv);
                        exit(-1);
		}

	} else if (pid != -1) {
		if (waitpid(pid, &res, 0) < 0) {
			fprintf(stderr, "exec_system_cmd waitpid: %s",
				strerror(errno));
			return -1;
		}
		return res;

	} else {
		fprintf(stderr, "exec_system_cmd fork: %s",
			strerror(errno));
		return -1;
	}

	return 0;	/* NO reach, suppress GCC warning */
}

/*
 * Extract the brief description text from system man output.
 *   E.g. "man ls" outputs "ls - list directory contents",
 *   then "list directory contents" will be returned.
 */
char *
get_man_desc(char *cmd, char *desc, int len)
{
	FILE	*fp;
	char	buf[256], pfx[64];
	char	*ptr = NULL, *text = NULL;
	int	num = 0;

	if (!cmd || !cmd[0] || !desc || len < 8) return NULL;

	bzero(desc, len);
	snprintf(buf, sizeof(buf), "man %s 2>/dev/null", cmd);
	snprintf(pfx, sizeof(pfx), "%s -", cmd);

	if ((fp = popen(buf, "r")) == NULL)
		return NULL;

	while (num++ < 10 && fgets(buf, sizeof(buf), fp)) {
		if (!(ptr = strstr(buf, pfx)))
			continue;
		ptr += strlen(pfx);
		while (*ptr && isspace(*ptr)) ptr++;
		if (*ptr && (text = strtok(ptr, "\r\n"))) {
			snprintf(desc, len, "%s", text);
			desc[0] = toupper(desc[0]);
			break;
		}
	}
	pclose(fp);
	return ((desc[0]) ? desc : NULL);
}

/*
 * Get absolute directory of the path, ugly by chdir & getcwd
 */
char *
get_abs_dir(char *path, char *abs_dir, int len)
{
	char	*dir;
	char	buf[PATH_MAX];
	char	cwd[PATH_MAX];
	struct stat stat_buf;

	if (!path || !path[0] || !abs_dir || len < 8)
		return NULL;

	snprintf(buf, sizeof(buf), "%s", path);
	if (stat(buf, &stat_buf) == 0 && S_ISDIR(stat_buf.st_mode))
		dir = buf;
	else if (!(dir = dirname(buf)))
		return NULL;

	if (!getcwd(cwd, sizeof(cwd))) return NULL;

	if (chdir(dir) != 0) {
		syslog(LOG_ERR, "get_abs_dir chdir '%s': %s",
		       dir, strerror(errno));
		return NULL;
	}

	if (!getcwd(abs_dir, len)) return NULL;

	/* getcwd does not append tailing '/', but wee need trailing '/'
	 * for acurate dir prefix match
	 */
	if (strlen(abs_dir) < len - 1) {
		strncat(abs_dir, "/", 1);
	} else {
		syslog(LOG_ERR, "get_abs_dir: no space to append '/' to '%s'",
		       dir);
		return NULL;
	}

	chdir(cwd);
	return abs_dir;
}