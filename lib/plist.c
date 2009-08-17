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

static prop_dictionary_t regpkgdb_dict;
static bool regpkgdb_initialized;

bool
xbps_add_obj_to_dict(prop_dictionary_t dict, prop_object_t obj,
		       const char *key)
{
	assert(dict != NULL);
	assert(obj != NULL);
	assert(key != NULL);

	if (!prop_dictionary_set(dict, key, obj)) {
		prop_object_release(dict);
		return false;
	}

	prop_object_release(obj);
	return true;
}

bool
xbps_add_obj_to_array(prop_array_t array, prop_object_t obj)
{
	assert(array != NULL);
	assert(obj != NULL);

	if (!prop_array_add(array, obj)) {
		prop_object_release(array);
		return false;
	}

	prop_object_release(obj);
	return true;
}

int
xbps_callback_array_iter_in_repolist(int (*fn)(prop_object_t, void *, bool *),
				     void *arg)
{
	prop_dictionary_t repolistd;
	const char *rootdir;
	char *plist;
	int rv = 0;

	assert(fn != NULL);

	rootdir = xbps_get_rootdir();
	plist = xbps_xasprintf("%s/%s/%s", rootdir,
	    XBPS_META_PATH, XBPS_REPOLIST);
	if (plist == NULL)
		return EINVAL;

	/*
	 * Get the dictionary with the list of registered repositories.
	 */
	repolistd = prop_dictionary_internalize_from_file(plist);
	if (repolistd == NULL)
                return EINVAL;

	/*
	 * Iterate over the repository pool and run the associated
	 * callback function. The loop is stopped when the bool
	 * argument is true or the cb returns non 0.
	 */
	rv = xbps_callback_array_iter_in_dict(repolistd, "repository-list",
		fn, arg);
	prop_object_release(repolistd);
	free(plist);

	return rv;
}

int
xbps_callback_array_iter_in_dict(prop_dictionary_t dict, const char *key,
				 int (*fn)(prop_object_t, void *, bool *),
				 void *arg)
{
	prop_object_iterator_t iter;
	prop_object_t obj;
	int rv = 0;
	bool cbloop_done = false;

	assert(dict != NULL);
	assert(key != NULL);
	assert(fn != NULL);

	iter = xbps_get_array_iter_from_dict(dict, key);
	if (iter == NULL)
		return EINVAL;

	while ((obj = prop_object_iterator_next(iter))) {
		rv = (*fn)(obj, arg, &cbloop_done);
		if (rv != 0 || cbloop_done)
			break;
	}

	prop_object_iterator_release(iter);
	return rv;
}

int
xbps_callback_array_iter_reverse_in_dict(prop_dictionary_t dict,
    const char *key, int (*fn)(prop_object_t, void *, bool *), void *arg)
{
	prop_array_t array;
	prop_object_t obj;
	int rv = 0;
	bool cbloop_done = false;
	unsigned int cnt = 0;

	assert(dict != NULL);
	assert(key != NULL);
	assert(fn != NULL);

	array = prop_dictionary_get(dict, key);
	if (array == NULL || prop_object_type(array) != PROP_TYPE_ARRAY)
		return EINVAL;

	if ((cnt = prop_array_count(array)) == 0)
		return 0;

	while (cnt--) {
		obj = prop_array_get(array, cnt);
		rv = (*fn)(obj, arg, &cbloop_done);
		if (rv != 0 || cbloop_done)
			break;
	}

	return rv;
}

prop_dictionary_t
xbps_find_pkg_from_plist(const char *plist, const char *pkgname)
{
	prop_dictionary_t dict, obj, res;

	assert(plist != NULL);
	assert(pkgname != NULL);

	dict = prop_dictionary_internalize_from_file(plist);
	if (dict == NULL) {
		errno = ENOENT;
		return NULL;
	}

	obj = xbps_find_pkg_in_dict(dict, "packages", pkgname);
	if (obj == NULL) {
		prop_object_release(dict);
		errno = ENOENT;
		return NULL;
	}

	res = prop_dictionary_copy(obj);
	prop_object_release(dict);

	return res;
}

prop_dictionary_t
xbps_find_pkg_installed_from_plist(const char *pkgname)
{
	prop_dictionary_t d, pkgd;
	pkg_state_t state = 0;

	d = xbps_prepare_regpkgdb_dict();
	if (d == NULL)
		return NULL;

	pkgd = xbps_find_pkg_in_dict(d, "packages", pkgname);
	if (pkgd == NULL)
		return NULL;

	if (xbps_get_pkg_state_installed(pkgname, &state) != 0)
		return NULL;

	switch (state) {
	case XBPS_PKG_STATE_INSTALLED:
	case XBPS_PKG_STATE_UNPACKED:
		return prop_dictionary_copy(pkgd);
	default:
		return NULL;
	}
}

prop_dictionary_t
xbps_find_pkg_in_dict(prop_dictionary_t dict, const char *key,
		      const char *pkgname)
{
	prop_object_iterator_t iter;
	prop_object_t obj;
	const char *dpkgn;

	assert(dict != NULL);
	assert(pkgname != NULL);
	assert(key != NULL);

	iter = xbps_get_array_iter_from_dict(dict, key);
	if (iter == NULL)
		return NULL;

	while ((obj = prop_object_iterator_next(iter))) {
		prop_dictionary_get_cstring_nocopy(obj, "pkgname", &dpkgn);
		if (strcmp(dpkgn, pkgname) == 0)
			break;
	}
	prop_object_iterator_release(iter);

	return obj;
}

prop_dictionary_t
xbps_prepare_regpkgdb_dict(void)
{
	const char *rootdir;
	char *plist;

	if (regpkgdb_initialized == false) {
		rootdir = xbps_get_rootdir();
		plist = xbps_xasprintf("%s/%s/%s", rootdir,
		    XBPS_META_PATH, XBPS_REGPKGDB);
		if (plist == NULL)
			return NULL;

		regpkgdb_dict = prop_dictionary_internalize_from_file(plist);
		if (regpkgdb_dict == NULL) {
			free(plist);
			return NULL;
		}
		free(plist);
		regpkgdb_initialized = true;
	}

	return regpkgdb_dict;
}

void
xbps_release_regpkgdb_dict(void)
{
	if (regpkgdb_initialized == false)
		return;

	prop_object_release(regpkgdb_dict);
	regpkgdb_dict = NULL;
	regpkgdb_initialized = false;
}

bool
xbps_find_string_in_array(prop_array_t array, const char *val)
{
	prop_object_iterator_t iter;
	prop_object_t obj;

	assert(array != NULL);
	assert(val != NULL);

	iter = prop_array_iterator(array);
	if (iter == NULL)
		return false;

	while ((obj = prop_object_iterator_next(iter)) != NULL) {
		if (prop_object_type(obj) != PROP_TYPE_STRING)
			continue;
		if (prop_string_equals_cstring(obj, val)) {
			prop_object_iterator_release(iter);
			return true;
		}
	}

	prop_object_iterator_release(iter);
	return false;
}

prop_object_iterator_t
xbps_get_array_iter_from_dict(prop_dictionary_t dict, const char *key)
{
	prop_array_t array;

	assert(dict != NULL);
	assert(key != NULL);

	array = prop_dictionary_get(dict, key);
	if (array == NULL || prop_object_type(array) != PROP_TYPE_ARRAY)
		return NULL;

	return prop_array_iterator(array);
}

int
xbps_remove_string_from_array(prop_array_t array, const char *str)
{
	prop_object_t obj;
	prop_object_iterator_t iter;
	size_t idx = 0;
	bool found = false;

	assert(array != NULL);
	assert(str != NULL);

	iter = prop_array_iterator(array);
	if (iter == NULL)
		return ENOMEM;

	while ((obj = prop_object_iterator_next(iter)) != NULL) {
		if (prop_object_type(obj) != PROP_TYPE_STRING)
			continue;
		if (prop_string_equals_cstring(obj, str)) {
			found = true;
			break;
		}
		idx++;
	}
	prop_object_iterator_release(iter);
	if (found == false)
		return ENOENT;

	prop_array_remove(array, idx);

	return 0;
}

int
xbps_remove_pkg_from_dict(prop_dictionary_t dict, const char *key,
			  const char *pkgname)
{
	prop_array_t array;
	prop_object_t obj;
	prop_object_iterator_t iter;
	const char *curpkgname;
	size_t i = 0;
	bool found = false;

	assert(dict != NULL);
	assert(key != NULL);
	assert(pkgname != NULL);

	array = prop_dictionary_get(dict, key);
	if (array == NULL || prop_object_type(array) != PROP_TYPE_ARRAY)
		return EINVAL;

	iter = prop_array_iterator(array);
	if (iter == NULL)
		return errno;

	/* Iterate over the array of dictionaries to find its index. */
	while ((obj = prop_object_iterator_next(iter))) {
		prop_dictionary_get_cstring_nocopy(obj, "pkgname",
		    &curpkgname);
		if ((curpkgname && (strcmp(curpkgname, pkgname) == 0))) {
			found = true;
			break;
		}
		i++;
	}
	prop_object_iterator_release(iter);
	if (found == true)
		prop_array_remove(array, i);
	else
		return ENOENT;

	return 0;
}

int
xbps_remove_pkg_dict_from_file(const char *pkg, const char *plist)
{
	prop_dictionary_t pdict;
	int rv = 0;

	assert(pkg != NULL);
	assert(plist != NULL);

	pdict = prop_dictionary_internalize_from_file(plist);
	if (pdict == NULL)
		return errno;

	rv = xbps_remove_pkg_from_dict(pdict, "packages", pkg);
	if (rv != 0) {
		prop_object_release(pdict);
		return rv;
	}

	if (!prop_dictionary_externalize_to_file(pdict, plist)) {
		prop_object_release(pdict);
		return errno;
	}

	prop_object_release(pdict);

	return 0;
}

prop_dictionary_t
xbps_read_dict_from_archive_entry(struct archive *ar,
				  struct archive_entry *entry)
{
	prop_dictionary_t d;
	size_t buflen = 0;
	ssize_t nbytes = -1;
	char *buf;

	assert(ar != NULL);
	assert(entry != NULL);

	buflen = (size_t)archive_entry_size(entry);
	buf = malloc(buflen);
	if (buf == NULL)
		return NULL;

	nbytes = archive_read_data(ar, buf, buflen);
	if ((size_t)nbytes != buflen) {
		free(buf);
		return NULL;
	}

	d = prop_dictionary_internalize(buf);
	free(buf);
	if (d == NULL)
		return NULL;
	else if (prop_object_type(d) != PROP_TYPE_DICTIONARY) {
		prop_object_release(d);
		return NULL;
	}

	return d;
}
