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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ocli/ocli.h>

#include "man.h"
#include "jsh.h"

#define MAX_MAN_ENT	512
#define MAX_MAN_OPT	32	/* including first keyword */

typedef struct man man_t;

static man_t	**man_table = NULL;
static int	man_size = 0;

/*
 * Get man entry by name
 */
static man_t *
get_man_ent(char *cmd)
{
	int	i;
	man_t	*ent;

	if (!man_table || man_size == 0) return NULL;
	for (i = 0; i < man_size; i++) {
		ent = man_table[i];
		if (ent && ent->cmd && strcmp(ent->cmd, cmd) == 0)
			return ent;
	}
	return NULL;
}

/*
 * Get man description line from auto generated man[] array in man.h
 */
static char *
get_auto_man_desc(char *cmd, char *desc, int len)
{
	struct man *ptr;
	int	n = 0;

	if ((n = (sizeof(man) / sizeof(struct man))) < 2) return NULL;

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
 * Get man description, try sys auto man first
 */
char *
get_man_desc(char *cmd, char *desc, int len)
{
	char	*res;
	man_t	*ent;

	if (!cmd || !cmd[0] || !desc || len < 4) return NULL;
	bzero(desc, len);

	if ((res = get_auto_man_desc(cmd, desc, len)) != NULL)
		return res;
	if (!(ent = get_man_ent(cmd)) || !ent->desc || !ent->desc[0])
		return NULL;

	snprintf(desc, len, "%s", ent->desc);
	desc[0] = toupper(desc[0]);
	return desc;
}

/*
 * Get opt description
 */
char *
get_man_opt_desc(char *cmd, char *opt, char *desc, int len)
{
	int	i;
	man_t	*ent, *ptr;

	if (!cmd || !cmd[0] || !opt || !opt[0] || !desc || len < 4)
		return NULL;

	bzero(desc, len);

	if (!(ent = get_man_ent(cmd)))
		return NULL;

	for (i = 1; i < MAX_MAN_OPT; i++) {
		ptr = ent + i;
		if (!ptr->cmd) break;
		if (strcmp(ptr->cmd, opt) == 0) {
			snprintf(desc, len, "%s", ptr->desc);
			desc[0] = toupper(desc[0]);
			return desc;
		}
	}
	return NULL;
}

/*
 * Mem utils of man entry
 */
static man_t *
malloc_man_ent()
{
	man_t	*ent;
	int	size = MAX_MAN_OPT  * sizeof(man_t);

	ent = (man_t *) malloc(size);
	if (!ent) {
		fprintf(stderr, "malloc_man_ent: %s\n", strerror(errno));
		return NULL;
	}
	bzero(ent, size);
	return ent;
}

static man_t *
new_man_ent()
{
	man_t	*ent;

	if (man_size >= MAX_MAN_ENT) return NULL;

	if ((ent = malloc_man_ent()) != NULL) {
		man_table[man_size++] = ent;
		return ent;
	} else
		return NULL;
}

static void
free_man_ent(man_t *ent)
{
	int	i;
	man_t	*ptr = NULL;

	if (!ent) return;
	for (i = 0; i < MAX_MAN_OPT; i++) {
		ptr = ent + i;
		if (!ptr->cmd) break;
		if (ptr->desc) free(ptr->desc);
		free(ptr->cmd);
	}
	free(ent);
}

/*
 * Free man table
 */
void
jsh_man_exit()
{
	int	i;

	if (!man_table) return;
	for (i = 0; i < man_size; i++) {
		if (man_table[i])
			free_man_ent(man_table[i]);
	}
	free(man_table);
}

/*
 * Init man table from man.conf
 */
int
jsh_man_init()
{
	FILE	*fp;
	char	buf[512];
	int	argc = 0;
	char	**argv = NULL;
	man_t	*cur_ent = NULL, *ent;
	int	cur_opt = 0;

	if ((fp = fopen(JSH_MAN_CONF, "r")) == NULL) {
		return -1;
	}

	man_table = (man_t **) malloc(MAX_MAN_ENT * sizeof(man_t *));
	if (!man_table) {
		fprintf(stderr, "jsh_man_init malloc: %s\n", strerror(errno));
		fclose(fp);
		return -1;
	}

	bzero(man_table, MAX_MAN_ENT * sizeof(man_t *));

	while (fgets(buf, sizeof(buf), fp)) {
		if ((argc = get_argv(buf, &argv, NULL)) < 1 ||
		    argv[0][0] == '#') {
			free_argv(argv);
			continue;
		}
		/* no indent: keyword, else: opt */
		if (!isspace(buf[0])) {
			if (!(cur_ent = new_man_ent())) {
				free_argv(argv);
				break;
			}
			cur_opt = 0;
			ent = cur_ent + cur_opt;
			ent->cmd = strdup(argv[0]);
			ent->desc = (argc >= 2) ? strdup(argv[1]) : NULL;
			cur_opt++;
		} else if (argc == 2 && cur_ent && cur_opt < MAX_MAN_OPT) {
			ent = cur_ent + cur_opt;
			ent->cmd = strdup(argv[0]);
			ent->desc = strdup(argv[1]);
			cur_opt++;
		}

		free_argv(argv);
	}

	fclose(fp);
	return 0;
}
