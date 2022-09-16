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

#ifndef _JSH_H
#define _JSH_H

#include <ocli/ocli.h>

/* Default jsh configuration directory */
#define JSH_CONF_DIR	"/usr/local/etc/jsh.d"

#define LEX_PATH	LEX_CUSTOM_TYPE(0)

extern int dir_helper(char *tok, char **matches, int limit);
extern int path_helper(char *tok, char **matches, int limit);

extern int is_path(char *str);
extern int is_captical_word(char *str);
extern int mylex_init(void);

extern int exec_system_cmd(char *cmd);
extern char *get_man_desc(char *cmd, char *desc, int len);
extern char *get_abs_dir(char *path, char *abs_dir, int len);

#endif	/* _JSH_H */
