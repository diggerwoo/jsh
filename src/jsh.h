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

#include <sys/types.h>
#include <ocli/ocli.h>

#if !defined(OCLI_VERSION_CODE) || OCLI_VERSION_CODE < OCLI_VERSION(0,91)
#  error "Libocli Version >= 0.91 is needed."
#endif

#define JSH_VERSION	"jsh 0.98"

extern char *home_dir;
extern char *sftp_server;

/* Default jsh configuration directory */
#define JSH_CONF_DIR	"/usr/local/etc/jsh.d"
#define SFTP_SERVER	"/usr/libexec/openssh/sftp-server"

#define LEX_PATH	LEX_CUSTOM_TYPE(0)

extern int dir_helper(char *tok, char **matches, int limit);
extern int path_helper(char *tok, char **matches, int limit);

extern int is_path(char *str);
extern int is_captical_word(char *str);
extern int mylex_init(void);

/* Mode of exec */
#define COMMON_EXEC	0
#define SH_CMD_EXEC	1
#define JAILED_EXEC	2
extern int exec_system_cmd(char *cmd, int mode);

extern char *get_man_desc(char *cmd, char *desc, int len);

extern char *get_basename(char *path);
extern char *get_abs_dir(char *path, char *abs_dir, int len, int full);
extern int in_subdir(char *ent, char *path);
extern int in_scp_homes(char *ent);
#define in_sftp_homes(x) in_scp_homes(x)

extern int jtrace(pid_t pid, int argc, char **argv);

#endif	/* _JSH_H */
