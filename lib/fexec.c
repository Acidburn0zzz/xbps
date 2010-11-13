/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matthias Scheler.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <xbps_api.h>
#include "xbps_api_impl.h"

/**
 * @file lib/fexec.c
 * @brief Generic file execution routines
 * @defgroup fexec File execution functions
 *
 * These functions will fork and execute a program in cwd (current working
 * directory), destination directory or changing root directory to
 * destination directory and passing an arbitrary number of arguments to it.
 */

static int
pfcexec(const char *path, const char *file, const char **argv)
{
	pid_t			child;
	int			status;
	bool			do_chroot = false;

	child = vfork();
	switch (child) {
	case 0:
		if (getuid() == 0 && access("./bin/sh", X_OK) == 0)
			do_chroot = true;

		/*
		 * If uid==0 and /bin/sh exists, we can change root directory,
		 * fork and execute the command. Otherwise just change current
		 * directory and fork/execute.
		 */
		if (path && do_chroot) {
			if (chroot(path) == -1)
				_exit(127);
			if (chdir("/") == -1)
				_exit(127);
		} else if (path && !do_chroot) {
			if (chdir(path) == -1)
				_exit(127);
		} else if (!path && do_chroot) {
			if (chroot(".") == -1)
				_exit(127);
			if (chdir("/") == -1)
				_exit(127);
		}

		(void)execvp(file, __UNCONST(argv));
		_exit(127);
		/* NOTREACHED */
	case -1:
		return -1;
	}

	while (waitpid(child, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}

static int
vfcexec(const char *path, int skipempty, const char *arg, va_list ap)
{
	const char **argv;
	size_t argv_size, argc;
	int retval;

	argv_size = 16;
	if ((argv = malloc(argv_size * sizeof(*argv))) == NULL) {
		errno = ENOMEM;
		return -1;
	}

	argv[0] = arg;
	argc = 1;

	do {
		if (argc == argv_size) {
			argv_size *= 2;
			argv = realloc(argv, argv_size * sizeof(*argv));
			if (argv == NULL) {
				errno = ENOMEM;
				return -1;
			}
		}

		arg = va_arg(ap, const char *);
		if (skipempty && arg && strlen(arg) == 0)
		    continue;

		argv[argc++] = arg;

	} while (arg != NULL);

	retval = pfcexec(path, argv[0], argv);
	free(argv);

	return retval;
}

int
xbps_file_exec(const char *arg, ...)
{
	va_list	ap;
	int	result;

	va_start(ap, arg);
	result = vfcexec(NULL, 0, arg, ap);
	va_end(ap);

	return result;
}

int
xbps_file_exec_skipempty(const char *arg, ...)
{
	va_list	ap;
	int	result;

	va_start(ap, arg);
	result = vfcexec(NULL, 1, arg, ap);
	va_end(ap);

	return result;
}

int
xbps_file_chdir_exec(const char *path, const char *arg, ...)
{
	va_list	ap;
	int	result;

	va_start(ap, arg);
	result = vfcexec(path, 0, arg, ap);
	va_end(ap);

	return result;
}
