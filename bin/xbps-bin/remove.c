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

void
xbps_autoremove_pkgs(void)
{
	prop_array_t orphans;
	prop_object_t obj;
	prop_object_iterator_t iter;
	const char *pkgname, *version;
	size_t cols = 0;
	int rv = 0;
	bool first = false;

	/*
	 * Removes orphan pkgs. These packages were installed
	 * as dependency and any installed package does not depend
	 * on it currently.
	 */

	orphans = xbps_find_orphan_packages();
	if (orphans == NULL)
		exit(EXIT_FAILURE);
	if (orphans != NULL && prop_array_count(orphans) == 0) {
		printf("There are not orphaned packages currently.\n");
		exit(EXIT_SUCCESS);
	}

	iter = prop_array_iterator(orphans);
	if (iter == NULL)
		goto out;

	printf("The following packages were installed automatically\n"
	    "(as dependencies) and aren't needed anymore:\n\n");
	while ((obj = prop_object_iterator_next(iter)) != NULL) {
		prop_dictionary_get_cstring_nocopy(obj, "pkgname", &pkgname);
		prop_dictionary_get_cstring_nocopy(obj, "version", &version);
		cols += strlen(pkgname) + strlen(version) + 4;
		if (cols <= 80) {
			if (first == false) {
				printf("  ");
				first = true;
			}
		} else {
			printf("\n  ");
			cols = strlen(pkgname) + strlen(version) + 4;
		}
		printf("%s-%s ", pkgname, version);
	}
	prop_object_iterator_reset(iter);
	printf("\n\n");

	if (xbps_noyes("Do you want to remove them?") == false) {
		printf("Cancelled!\n");
		goto out2;
	}

	while ((obj = prop_object_iterator_next(iter)) != NULL) {
		prop_dictionary_get_cstring_nocopy(obj, "pkgname", &pkgname);
		prop_dictionary_get_cstring_nocopy(obj, "version", &version);

		printf("Removing package %s-%s ...\n", pkgname, version);
		if ((rv = xbps_remove_pkg(pkgname, version, false)) != 0)
			goto out2;
	}
out2:
	prop_object_iterator_release(iter);
out:
	prop_object_release(orphans);
	if (rv != 0)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}

void
xbps_remove_installed_pkg(const char *pkgname, bool force)
{
	prop_array_t reqby;
	prop_dictionary_t dict;
	const char *version;
	int rv = 0;

	/*
	 * First check if package is required by other packages.
	 */
	dict = xbps_find_pkg_installed_from_plist(pkgname);
	if (dict == NULL) {
		printf("Package %s is not installed.\n", pkgname);
		goto out;
	}
	prop_dictionary_get_cstring_nocopy(dict, "version", &version);

	reqby = prop_dictionary_get(dict, "requiredby");
	if (reqby != NULL && prop_array_count(reqby) > 0) {
		printf("WARNING! %s-%s is required by the following "
		    "packages:\n\n", pkgname, version);
		(void)xbps_callback_array_iter_in_dict(dict,
			"requiredby", list_strings_in_array, NULL);
		printf("\n\n");
		if (!force) {
			if (!xbps_noyes("Do you want to remove %s?", pkgname)) {
				printf("Cancelling!\n");
				goto out;
			}
		}
		printf("Forcing %s-%s for deletion!\n", pkgname, version);
	} else {
		if (!force) {
			if (!xbps_noyes("Do you want to remove %s?", pkgname)) {
				printf("Cancelling!\n");
				goto out;
			}
		}
	}

	printf("Removing package %s-%s ...\n", pkgname, version);
	if ((rv = xbps_remove_pkg(pkgname, version, false)) != 0) {
		printf("Unable to remove %s-%s (%s).\n",
		    pkgname, version, strerror(errno));
		goto out;
	}

out:
	xbps_release_regpkgdb_dict();
	if (rv != 0)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}
