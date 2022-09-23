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
 * Currently only support the jailing of sftp-server
 */

#include <stdio.h>
#include <limits.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <syslog.h>
#include <sys/ptrace.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/user.h>
#include <asm/unistd.h>
#include <sys/reg.h>

#include "jsh.h"

#define FFLAG(f, x) ((f & x) == x)

static int jtrace_sftp_server(pid_t pid, int argc, char **argv);

static int along_sftp_home(char *ent);
static char *get_callname(long nr);
static int jtrace_xlat_flags(int flags, char *buf, int buflen);
static int jtrace_get_string(pid_t pid, char *addr, char *buf, int buflen);

/*
 * Jailed tracing entry
 */
int
jtrace(pid_t pid, int argc, char **argv)
{
	if (argc >= 1 && argv[0] &&
	    strcmp(argv[0], sftp_server) == 0) {
		syslog(LOG_DEBUG, "Start tracing %s", sftp_server);
		return jtrace_sftp_server(pid, argc, argv);
	}

	return -1;
}

/*
 * Jailed tracing specific for sftp-server
 */
static int
jtrace_sftp_server(pid_t pid, int argc, char **argv)
{
	struct user_regs_struct regs;

	char	*path;
	int	flags;
	char	path_info[256], path_dest[256];
	char	flags_info[64];
	char	call_info[512];
	int	status = 0;
	int	res = 0;

	/*
	 * injail: Set TURE if (lstat/stat/openat, open WR, or
	 * 		        mkdir/rmdir/link/unlink/rename) called
	 * incall: Set TURE if in lstat/stat/openat/open syscalls
	 */
	int	injail = 0;
	int	incall = 0;
	int	block = 0;
	char	last_path[256];

	bzero(path_info, sizeof(path_info));
	bzero(last_path, sizeof(last_path));

	while (1) {
		ptrace(PTRACE_SYSCALL, pid, NULL, NULL);

		if (waitpid(pid, &status, 0) < 0) {
			fprintf(stderr, "jtrace-sftp waitpid: %s",
				strerror(errno));
			return -1;
		}
		if (WIFEXITED(status)) break;

		if ((res = ptrace(PTRACE_GETREGS, pid, NULL, &regs)) < 0) {
			syslog(LOG_ERR, "jtrace-sftp getregs: %s\n", strerror(errno));
			break;
		}

		if (regs.orig_rax == __NR_open || regs.orig_rax == __NR_openat) {
			if (incall == 0) {
				incall = 1;

				if (regs.orig_rax == __NR_open) {
					path = (char *) regs.rdi;
					flags = regs.rsi;
				} else {
					path = (char *) regs.rsi;
					flags = regs.rdx;
				}

				jtrace_get_string(pid, path, path_info, sizeof(path_info)-1);
				jtrace_xlat_flags(flags, flags_info, sizeof(flags_info));

				snprintf(call_info, sizeof(call_info),
					"%s(\"%s\", %s <0x%x>)",
					get_callname(regs.orig_rax),
					path_info[0] ? path_info:"n/a", flags_info, flags);

				/* Bypass opening /dev/null for RDWR during initialization */
				if (!injail && strncmp(path_info, "/dev/", 5) &&
				    (regs.orig_rax == __NR_openat ||
				     FFLAG(flags, O_WRONLY) || FFLAG(flags, O_RDWR))) {
					injail = 1;
					syslog(LOG_DEBUG, "injail triggered by %s\n", call_info);
				}

				if (injail) {
					block = 0;
					/* Bypass opening without precedent stat/lstat
					 */
					if (strcmp(path_info, last_path) != 0 &&
					    (strncmp(path_info, "/etc/", 5) == 0 ||
					     strncmp(path_info, "/lib64/", 7) == 0 ||
					     strncmp(path_info, "/lib/", 5) == 0 ||
					     strncmp(path_info, "/usr/lib/", 9) == 0)) {
						syslog(LOG_DEBUG, "bypass %s\n", call_info);
					} else
					/*
					 * Limit opening dirs along with home dirs.
					 * Limit opening files inside home dirs.
					 */
					if (regs.orig_rax == __NR_openat &&
					    (flags & O_DIRECTORY) == O_DIRECTORY) {
						if (!along_sftp_home(path_info)) {
							syslog(LOG_DEBUG, "deny %s\n", call_info);
							regs.orig_rax = -1;
							if ((res = ptrace(PTRACE_SETREGS, pid, 0, &regs)) < 0) {
								syslog(LOG_ERR, "jtrace-sftp deny %s: %s\n",
									call_info, strerror(errno));
								break;
							}
							block = 1;
						}
					} else {
						if (!in_sftp_homes(path_info)) {
							syslog(LOG_DEBUG, "deny %s\n", call_info);
							regs.orig_rax = -1;
							if ((res = ptrace(PTRACE_SETREGS, pid, 0, &regs)) < 0) {
								syslog(LOG_ERR, "jtrace-sftp deny %s: %s\n",
									call_info, strerror(errno));
								break;
							}
							block = 1;
						}
					}
				}
			} else {
				incall = 0;
				syslog(LOG_DEBUG, "%s = %lld\n", call_info, regs.rax);
			}

		} else if (regs.orig_rax == __NR_stat || regs.orig_rax == __NR_lstat) {

			if (incall == 0) {
				incall = 1;

				jtrace_get_string(pid, (char *) regs.rdi, path_info, sizeof(path_info)-1);
				snprintf(call_info, sizeof(call_info),
					"%s(\"%s\")",
					get_callname(regs.orig_rax),
					path_info[0] ? path_info:"n/a");

				/* Bypass stating paths during sftp-server initialization.
				 * These stat calls might be trigerred by SELINUX or PAM.
				 */
				if (!injail &&
				    strncmp(path_info, "/etc/sysconfig/", 15) &&
				    strncmp(path_info, "/sys/", 5)) {
					injail = 1;
					syslog(LOG_DEBUG, "injail triggered by %s\n", call_info);
				}

			} else {
				incall = 0;
				snprintf(last_path, sizeof(last_path), "%s", path_info);
				/*
				 * sftp-server uses stat/lstat to assert the directory,
				 * and uses /etc/localtime to get server side timezone
				 */
				if (injail) {
					if (strcmp(path_info, "/etc/localtime") != 0 &&
					    !along_sftp_home(path_info)) {
						regs.rax = -EACCES;
						if ((res = ptrace(PTRACE_SETREGS, pid, 0, &regs)) < 0) {
							syslog(LOG_ERR, "jtrace-sftp block %s: %s\n",
								call_info, strerror(errno));
							break;
						}
						syslog(LOG_DEBUG, "block: %s = %lld\n", call_info, regs.rax);
					}
				} else {
					syslog(LOG_DEBUG, "%s = %lld\n", call_info, regs.rax);
				}
			}

		} else if (regs.orig_rax == __NR_mkdir || regs.orig_rax == __NR_rmdir ||
			   regs.orig_rax == __NR_unlink || regs.orig_rax == __NR_link || 
			   regs.orig_rax == __NR_rename) {

			if (incall == 0) {
				incall = 1;

				jtrace_get_string(pid, (char *) regs.rdi, path_info, sizeof(path_info)-1);
				bzero(path_dest, sizeof(path_dest));

				if (regs.orig_rax == __NR_link || regs.orig_rax == __NR_rename) {
					jtrace_get_string(pid, (char *) regs.rsi, path_dest, sizeof(path_dest)-1);
					snprintf(call_info, sizeof(call_info),
						"%s(\"%s\", \"%s\")",
						get_callname(regs.orig_rax),
						path_info[0] ? path_info:"n/a",
						path_dest[0] ? path_dest:"n/a");
				} else {
					snprintf(call_info, sizeof(call_info),
						"%s(\"%s\")",
						get_callname(regs.orig_rax),
						path_info[0] ? path_info:"n/a");
				}

				if (!injail) {
					injail = 1;
					syslog(LOG_DEBUG, "injail triggered by %s\n", call_info);
				}

				/* limit mkdir/rmdir/unlink/link/rename inside home dirs */
				if (injail) {
					block = 0;
					if (!in_sftp_homes(path_info) ||
					    (path_dest[0] && !in_sftp_homes(path_dest))) {
						syslog(LOG_DEBUG, "deny %s\n", call_info);
						regs.orig_rax = -1;
						if ((res = ptrace(PTRACE_SETREGS, pid, 0, &regs)) < 0) {
							syslog(LOG_ERR, "jtrace-sftp deny %s: %s\n",
								call_info, strerror(errno));
							break;
						}
						block = 1;
					}
				}

			} else {
				incall = 0;
				syslog(LOG_DEBUG, "%s = %lld\n", call_info, regs.rax);
			}

		} else if (regs.orig_rax == -1) {
			incall = 0;
			if (block) {
				regs.rax = -EACCES;
				if ((res = ptrace(PTRACE_SETREGS, pid, 0, &regs)) < 0) {
					syslog(LOG_ERR, "jtrace-sftp redirect %s: %s\n",
						call_info, strerror(errno));
					break;
				}
				syslog(LOG_DEBUG, "redirect: %s = %lld\n", call_info, regs.rax);
				block = 0;
			}
		}
	}

	return res;
}

/*
 * Test if dirent is along each other with any home dirs
 */
static int 
along_sftp_home(char *ent)
{
	char	*dir, *path;
	char	abs_dir[PATH_MAX];
	char	prefix[PATH_MAX];
	char	buf[512];

	if (!ent || !*ent) return 0;

	if (!get_abs_dir(ent, abs_dir, sizeof(abs_dir), 1))
		return 0;

	syslog(LOG_DEBUG, "get abs full '%s'=>'%s'\n", ent, abs_dir);

	/* along each other with home_dir */
	if (strncmp(abs_dir, home_dir, strlen(home_dir)) == 0 ||
	    strncmp(abs_dir, home_dir, strlen(abs_dir)) == 0)
		return 1;

	/* along each other with SCPDIR entries */
	if (!(path = getenv("SCPDIR"))) return 0;

	snprintf(buf, sizeof(buf), "%s", path);
	dir = strtok(buf, ":");
	while (dir) {
		if (!*dir) continue;
		if (dir[strlen(dir) - 1] != '/')
			snprintf(prefix, sizeof(prefix), "%s/", dir);
		else
			snprintf(prefix, sizeof(prefix), "%s", dir);
		if (strncmp(abs_dir, prefix, strlen(prefix)) == 0 ||
		    strncmp(abs_dir, prefix, strlen(abs_dir)) == 0) {
			return 1;
		}
		dir = strtok(NULL, ":");
	}
	syslog(LOG_DEBUG, "'%s'=>'%s' is not along with any home dirs\n",
	       ent, abs_dir);
	return 0;
}

/*
 * Get syscall name by number
 */
static char *
get_callname(long nr)
{
	switch (nr) {
	case __NR_open:
		return "open";
	case __NR_openat:
		return "openat";
	case __NR_stat:
		return "stat";
	case __NR_lstat:
		return "lstat";
	case __NR_mkdir:
		return "mkdir";
	case __NR_rmdir:
		return "rmdir";
	case __NR_unlink:
		return "unlink";
	case __NR_link:
		return "link";
	case __NR_rename:
		return "rename";
	default:
		break;
	}
	return "n/a";
}

/*
 * Translate flags of open syscalls
 */
static int
jtrace_xlat_flags(int flags, char *buf, int buflen)
{
	char	*ptr;
	int	len, left;

	if (!buf || buflen < 8) return 0;
	bzero(buf, buflen);

	ptr = buf;
	left = buflen;

	if (FFLAG(flags, O_RDWR))
		ptr += (len = snprintf(ptr, left, "O_RDWR"));
	else if (FFLAG(flags, O_WRONLY))
		ptr += (len = snprintf(ptr, left, "O_WRONLY"));
	else if (FFLAG(flags, O_RDONLY))
		ptr += (len = snprintf(ptr, left, "O_RDONLY"));
	else
		ptr += (len = snprintf(ptr, left, "N/A"));

	if ((left -= len) < 0) return buflen;

	if (FFLAG(flags, O_CLOEXEC)) {
		ptr += (len = snprintf(ptr, left, "|O_CLOEXEC"));
		if ((left -= len) < 0) return buflen;
	}

	if (FFLAG(flags, O_DIRECTORY)) {
		ptr += (len = snprintf(ptr, left, "|O_DIRECTORY"));
		if ((left -= len) < 0) return buflen;
	}
	return buflen - left;
}

/*
 * Get NULL terminated string from tracee addr
 */
static int
jtrace_get_string(pid_t pid, char *addr, char *buf, int buflen)
{
	char	*ptr;
	int	i = 0, offset = 0;

	if (!buf || buflen < sizeof(long)) return 0;
	bzero(buf, buflen);

	ptr = buf;
	offset = ptr - buf;
	while (buflen - offset >= sizeof(long)) {
		/* Clear errno as suggested by man ptrace */
		errno = 0;
		*((long *) ptr) = ptrace(PTRACE_PEEKDATA, pid, addr + offset);
		if (errno) break;

		for (i = 0; i < sizeof(long); i++) {
			if (!*(ptr + i))
			return (offset + i);
		}
		ptr += sizeof(long);
		offset += sizeof(long);
	}
	return offset;
}
