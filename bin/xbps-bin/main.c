/*-
 * Copyright (c) 2008-2009 Juan Romero Pardines.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <xbps_api.h>
#include "defs.h"
#include "../xbps-repo/util.h"

static void	usage(void);
static int	list_pkgs_in_dict(prop_object_t, void *, bool *);

static void
usage(void)
{
	printf("Usage: xbps-bin [options] [target] [arguments]\n\n"
	" Available targets:\n"
        "    autoremove, autoupdate, check, files, install, list\n"
	"    purge, remove, show, update\n"
	" Targets with arguments:\n"
	"    check\t<pkgname>\n"
	"    files\t<pkgname>\n"
	"    install\t<pkgname>\n"
	"    purge\t[<pkgname>|<all>]\n"
	"    reconfigure\t[<pkgname>|<all>]\n"
	"    remove\t<pkgname>\n"
	"    show\t<pkgname>\n"
	"    update\t<pkgname>\n"
	" Options shared by all targets:\n"
	"    -r\t\t<rootdir>\n"
	"    -v\t\t<verbose>\n"
	" Options used by the (auto)remove and install target:\n"
	"    -f\t\tForce installation or removal of packages.\n"
	"      \t\tBeware with this option if you use autoremove!\n"
	"\n");
	exit(EXIT_FAILURE);
}

static int
list_pkgs_in_dict(prop_object_t obj, void *arg, bool *loop_done)
{
	const char *pkgname, *version, *short_desc;

	(void)arg;
	(void)loop_done;

	assert(prop_object_type(obj) == PROP_TYPE_DICTIONARY);

	prop_dictionary_get_cstring_nocopy(obj, "pkgname", &pkgname);
	prop_dictionary_get_cstring_nocopy(obj, "version", &version);
	prop_dictionary_get_cstring_nocopy(obj, "short_desc", &short_desc);
	if (pkgname && version && short_desc) {
		printf("%s-%s\t%s\n", pkgname, version, short_desc);
		return 0;
	}

	return EINVAL;
}

int
main(int argc, char **argv)
{
	prop_dictionary_t dict;
	prop_object_t obj;
	prop_object_iterator_t iter;
	const char *curpkgname;
	int c, flags = 0, rv = 0;
	bool force = false, verbose = false;

	while ((c = getopt(argc, argv, "Cfr:v")) != -1) {
		switch (c) {
		case 'f':
			flags |= XBPS_FLAG_FORCE;
			force = true;
			break;
		case 'r':
			/* To specify the root directory */
			xbps_set_rootdir(optarg);
			break;
		case 'v':
			verbose = true;
			flags |= XBPS_FLAG_VERBOSE;
			break;
		case '?':
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	if (flags != 0)
		xbps_set_flags(flags);

	if ((dict = xbps_prepare_regpkgdb_dict()) == NULL) {
		if (errno != ENOENT) {
			rv = errno;
			printf("Couldn't initialized regpkgdb dict: %s\n",
			    strerror(errno));
			goto out;
		}
	}

	if (strcasecmp(argv[0], "list") == 0) {
		/* Lists packages currently registered in database. */
		if (argc != 1)
			usage();

		if (dict == NULL) {
			printf("No packages currently installed.\n");
			goto out;
		}

		if (!xbps_callback_array_iter_in_dict(dict, "packages",
		    list_pkgs_in_dict, NULL)) {
			rv = errno;
			goto out;
		}

	} else if (strcasecmp(argv[0], "install") == 0) {
		/* Installs a binary package and required deps. */
		if (argc != 2)
			usage();

		xbps_install_pkg(argv[1], force, false);

	} else if (strcasecmp(argv[0], "update") == 0) {
		/* Update an installed package. */
		if (argc != 2)
			usage();

		xbps_install_pkg(argv[1], force, true);

	} else if (strcasecmp(argv[0], "remove") == 0) {
		/* Removes a binary package. */
		if (argc != 2)
			usage();

		xbps_remove_installed_pkg(argv[1], force);

	} else if (strcasecmp(argv[0], "show") == 0) {
		/* Shows info about an installed binary package. */
		if (argc != 2)
			usage();

		rv = show_pkg_info_from_metadir(argv[1]);
		if (rv != 0) {
			printf("Package %s not installed.\n", argv[1]);
			exit(EXIT_FAILURE);
		}

	} else if (strcasecmp(argv[0], "files") == 0) {
		/* Shows files installed by a binary package. */
		if (argc != 2)
			usage();

		rv = show_pkg_files_from_metadir(argv[1]);
		if (rv != 0) {
			printf("Package %s not installed.\n", argv[1]);
			exit(EXIT_FAILURE);
		}

	} else if (strcasecmp(argv[0], "check") == 0) {
		/* Checks the integrity of an installed package. */
		if (argc != 2)
			usage();

		rv = xbps_check_pkg_integrity(argv[1]);

	} else if (strcasecmp(argv[0], "autoupdate") == 0) {
		/*
		 * To update all packages currently installed.
		 */
		if (argc != 1)
			usage();

		xbps_autoupdate_pkgs(force);

	} else if (strcasecmp(argv[0], "autoremove") == 0) {
		/*
		 * Removes orphan pkgs. These packages were installed
		 * as dependency and any installed package does not depend
		 * on it currently.
		 */
		if (argc != 1)
			usage();

		xbps_autoremove_pkgs();

	} else if (strcasecmp(argv[0], "purge") == 0) {
		/*
		 * Purge a package completely.
		 */
		if (argc != 2)
			usage();

		if (strcasecmp(argv[1], "all") == 0) {
			iter = xbps_get_array_iter_from_dict(dict, "packages");
			if (iter == NULL)
				goto out;

			while ((obj = prop_object_iterator_next(iter))) {
				prop_dictionary_get_cstring_nocopy(obj,
				    "pkgname", &curpkgname);
				if ((rv = xbps_purge_pkg(curpkgname)) != 0)
					break;
			}
			prop_object_iterator_release(iter);
		} else {
			rv = xbps_purge_pkg(argv[1]);
		}

	} else if (strcasecmp(argv[0], "reconfigure") == 0) {
		/*
		 * Reconfigure a package.
		 */
		if (argc != 2)
			usage();

		if (strcasecmp(argv[1], "all") == 0) {
			iter = xbps_get_array_iter_from_dict(dict, "packages");
			if (iter == NULL)
				goto out;

			while ((obj = prop_object_iterator_next(iter))) {
				prop_dictionary_get_cstring_nocopy(obj,
				    "pkgname", &curpkgname);
				if ((rv = xbps_configure_pkg(curpkgname)) != 0)
					break;
			}
			prop_object_iterator_release(iter);
		} else {
			rv = xbps_configure_pkg(argv[1]);
		}

	} else {
		usage();
	}

out:
	xbps_release_regpkgdb_dict();
	if (rv != 0)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}
