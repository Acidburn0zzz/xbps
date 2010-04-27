/*-
 * Copyright (c) 2008-2010 Juan Romero Pardines.
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <xbps_api.h>

/**
 * @file lib/repository.c
 * @brief Repository registration routines
 * @defgroup repo_register Repository registration functions
 */

int
xbps_repository_register(const char *uri)
{
	prop_dictionary_t dict;
	prop_array_t array;
	prop_object_t obj = NULL;
	char *plist;
	int rv = 0;

	assert(uri != NULL);

	plist = xbps_xasprintf("%s/%s/%s", xbps_get_rootdir(),
	    XBPS_META_PATH, XBPS_REPOLIST);
	if (plist == NULL)
		return errno;

	/* First check if we have the repository plist file. */
	dict = prop_dictionary_internalize_from_zfile(plist);
	if (dict == NULL) {
		/* Looks like not, create it. */
		dict = prop_dictionary_create();
		if (dict == NULL) {
			free(plist);
			return errno;
		}
		/* Create the array and add the repository URI on it. */
		array = prop_array_create();
		if (array == NULL) {
			rv = errno;
			goto out;
		}
		if (!prop_array_set_cstring_nocopy(array, 0, uri)) {
			rv = errno;
			goto out;
		}
		/* Add the array obj into the main dictionary. */
		if (!xbps_add_obj_to_dict(dict, array, "repository-list")) {
			rv = errno;
			goto out;
		}
	} else {
		/* Append into the array, the plist file exists. */
		array = prop_dictionary_get(dict, "repository-list");
		if (array == NULL) {
			rv = errno;
			goto out;
		}
		/* It seems that this object is already there */
		if (xbps_find_string_in_array(array, uri)) {
			rv = EEXIST;
			goto out;
		}

		obj = prop_string_create_cstring(uri);
		if (!xbps_add_obj_to_array(array, obj)) {
			prop_object_release(obj);
			rv = errno;
			goto out;
		}
	}

	/* Write dictionary into plist file. */
	if (!prop_dictionary_externalize_to_zfile(dict, plist)) {
		if (obj)
			prop_object_release(obj);
		rv = errno;
		goto out;
	}

out:
	prop_object_release(dict);
	free(plist);

	return rv;
}

int
xbps_repository_unregister(const char *uri)
{
	prop_dictionary_t dict;
	prop_array_t array;
	char *plist;
	int rv = 0;

	assert(uri != NULL);

	plist = xbps_xasprintf("%s/%s/%s", xbps_get_rootdir(),
	    XBPS_META_PATH, XBPS_REPOLIST);
	if (plist == NULL)
		return errno;

	dict = prop_dictionary_internalize_from_zfile(plist);
	if (dict == NULL) {
		free(plist);
		return errno;
	}

	array = prop_dictionary_get(dict, "repository-list");
	if (array == NULL) {
		rv = errno;
		goto out;
	}

	rv = xbps_remove_string_from_array(array, uri);
	if (rv == 0) {
		/* Update plist file. */
		if (!prop_dictionary_externalize_to_zfile(dict, plist))
			rv = errno;
	}

out:
	prop_object_release(dict);
	free(plist);

	return rv;
}
