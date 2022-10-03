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

/*
 * This module implements customized completion helpers and lexcical
 * extensions specically for jsh.
 *
 * Refer to the API manual of libocli:
 *   https://github.com/diggerwoo/libocli/blob/main/doc/README.md
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "jsh.h"

/*
 * Auto completion generator for path match
 */
static int
path_generator(char *tok, char **matches, int limit, int st_mask)
{
	DIR	*dptr;
	struct dirent *dent;
	int	ignore_ddots = 1, n = 0;

	char	dir[256] = "";
	char	*base = NULL, *ptr = NULL;
	int	dir_len = 0, base_len = 0;

	char	path[PATH_MAX];
	struct stat stat_buf;

	if (tok && tok[0]) {
		if ((ptr = strrchr(tok, '/'))) {
			dir_len = ptr - tok + 1;
			if (dir_len >= sizeof(dir)) return 0;
			strncpy(dir, tok, dir_len);

			if (*(ptr + 1)) {
				base = ptr + 1;
				base_len = strlen(base);
			}
		} else {
			base = tok;
			base_len = strlen(tok);
		}
	}

	/* Like bash, prompt "." or ".." only if basename is "." or ".." */
	if (base && (strcmp(base, ".") == 0 || strcmp(base, "..") == 0))
		ignore_ddots = 0;

	if (!(dptr = opendir(dir[0] ? dir:"./"))) return 0;
	while (n < limit && (dent = readdir(dptr))) {
		if (ignore_ddots &&
		    (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0))
			continue;
		if ((!base || strncmp(base, dent->d_name, base_len) == 0)) {
			ptr = &path[1];
			snprintf(ptr, sizeof(path)-2, "%s%s", dir, dent->d_name);
			if (home_jailed && !along_home_dirs(ptr)) continue;
			if (stat(ptr, &stat_buf) != 0) continue;
			if (st_mask && (stat_buf.st_mode & st_mask) != st_mask) continue;
			if (S_ISDIR(stat_buf.st_mode)) {
				path[0] = '^';
				strncat(path, "/", 1);
				matches[n++] = strdup(path);
			} else {
				matches[n++] = strdup(ptr);
			}
		}
	}
	closedir(dptr);
	return n;
}

/*
 * Completion helper to generate directory tokens only
 */
int
dir_helper(char *tok, char **matches, int limit)
{
	return path_generator(tok, matches, limit, S_IFDIR);
}

/*
 * Completion helper to generate directory or file tokens
 */
int
path_helper(char *tok, char **matches, int limit)
{
	return path_generator(tok, matches, limit, 0);
}

/*
 * If word is capitalized in jsh conf, it is considered a lex name.
 */
int
is_captical_word(char *str)
{
	int	upper = 0;
	if (!str || !str[0]) return 0;
	while (*str) {
		if (isupper(*str))
			upper++;
		else if (islower(*str))
			return 0;
		str++;
	}
	return (upper > 0);
}

/*
 * A loose path lex allows ./ ../ and space
 */
int
is_path(char *str)
{
	int	res;

	if (!str || !str[0]) return 0;

	res = pcre_custom_match(str, LEX_PATH,
				"^(\\/)|((\\/)?((\\.\\/)|(\\.\\.\\/')|([\\w|\\-\\. *]+[\\/]?))+)$");

	return (res == 1);
}

/*
 * The lexical name PATH is registered for jsh conf.
 */
int
mylex_init()
{
	set_custom_lex_ent(LEX_PATH, "PATH", is_path, "Path name", NULL);
	return 0;
}
