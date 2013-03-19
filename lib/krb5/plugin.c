/*
 * Copyright (c) 2006 - 2007 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "krb5_locl.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#include <dirent.h>

struct krb5_plugin {
    void *symbol;
    struct krb5_plugin *next;
};

struct plugin {
    enum { DSO, SYMBOL } type;
    union {
	struct {
	    char *path;
	    void *dsohandle;
	} dso;
	struct {
	    enum krb5_plugin_type type;
	    char *name;
	    char *symbol;
	} symbol;
    } u;
    struct plugin *next;
};

static HEIMDAL_MUTEX plugin_mutex = HEIMDAL_MUTEX_INITIALIZER;
static struct plugin *registered = NULL;
static int plugins_needs_scan = 1;

static const char *sysplugin_dirs[] =  { 
    LIBDIR "/plugin/krb5",
#ifdef __APPLE__
    "/Library/KerberosPlugins/KerberosFrameworkPlugins",
    "/System/Library/KerberosPlugins/KerberosFrameworkPlugins",
#endif
    NULL
};

/*
 *
 */

void *
_krb5_plugin_get_symbol(struct krb5_plugin *p)
{
    return p->symbol;
}

struct krb5_plugin *
_krb5_plugin_get_next(struct krb5_plugin *p)
{
    return p->next;
}

/*
 *
 */

#ifdef HAVE_DLOPEN

static krb5_error_code
loadlib(krb5_context context, char *path)
{
    struct plugin *e;

    e = calloc(1, sizeof(*e));
    if (e == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	free(path);
	return ENOMEM;
    }

#ifndef RTLD_LAZY
#define RTLD_LAZY 0
#endif
#ifndef RTLD_LOCAL
#define RTLD_LOCAL 0
#endif
    e->type = DSO;
    /* ignore error from dlopen, and just keep it as negative cache entry */
    e->u.dso.dsohandle = dlopen(path, RTLD_LOCAL|RTLD_LAZY);
    e->u.dso.path = path;

    e->next = registered;
    registered = e;

    return 0;
}
#endif /* HAVE_DLOPEN */

/**
 * Register a plugin symbol name of specific type.
 * @param context a Keberos context
 * @param type type of plugin symbol
 * @param name name of plugin symbol
 * @param symbol a pointer to the named symbol
 * @return In case of error a non zero error com_err error is returned
 * and the Kerberos error string is set.
 *
 * @ingroup krb5_support
 */

krb5_error_code
krb5_plugin_register(krb5_context context,
		     enum krb5_plugin_type type,
		     const char *name,
		     void *symbol)
{
    struct plugin *e;

    HEIMDAL_MUTEX_lock(&plugin_mutex);

    /* check for duplicates */
    for (e = registered; e != NULL; e = e->next) {
	if (e->type == SYMBOL &&
	    strcmp(e->u.symbol.name, name) == 0 &&
	    e->u.symbol.type == type && e->u.symbol.symbol == symbol) {
	    HEIMDAL_MUTEX_unlock(&plugin_mutex);
	    return 0;
	}
    }

    e = calloc(1, sizeof(*e));
    if (e == NULL) {
	HEIMDAL_MUTEX_unlock(&plugin_mutex);
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }
    e->type = SYMBOL;
    e->u.symbol.type = type;
    e->u.symbol.name = strdup(name);
    if (e->u.symbol.name == NULL) {
	HEIMDAL_MUTEX_unlock(&plugin_mutex);
	free(e);
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }
    e->u.symbol.symbol = symbol;

    e->next = registered;
    registered = e;
    HEIMDAL_MUTEX_unlock(&plugin_mutex);

    return 0;
}

static krb5_error_code
load_plugins(krb5_context context)
{
    struct plugin *e;
    krb5_error_code ret;
    char **dirs = NULL, **di;
    struct dirent *entry;
    char *path;
    DIR *d = NULL;

    if (!plugins_needs_scan)
	return 0;
    plugins_needs_scan = 0;

#ifdef HAVE_DLOPEN

    dirs = krb5_config_get_strings(context, NULL, "libdefaults",
				   "plugin_dir", NULL);
    if (dirs == NULL)
	dirs = rk_UNCONST(sysplugin_dirs);

    for (di = dirs; *di != NULL; di++) {
#ifdef KRB5_USE_PATH_TOKENS
	{
	    char * dir = NULL;

	    if (_krb5_expand_path_tokens(context, *di, &dir))
		continue;
	    d = opendir(dir);

	    free(dir);
	}
#else
	d = opendir(*di);
#endif
	if (d == NULL)
	    continue;
	rk_cloexec_dir(d);

	while ((entry = readdir(d)) != NULL) {
	    char *n = entry->d_name;

	    /* skip . and .. */
	    if (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')))
		continue;

	    path = NULL;
	    ret = 0;
#ifdef __APPLE__
	    { /* support loading bundles on MacOS */
		size_t len = strlen(n);
		if (len > 7 && strcmp(&n[len - 7],  ".bundle") == 0)
		    ret = asprintf(&path, "%s/%s/Contents/MacOS/%.*s", *di, n, (int)(len - 7), n);
	    }
#endif
	    if (ret < 0 || path == NULL)
		ret = asprintf(&path, "%s/%s", *di, n);

	    if (ret < 0 || path == NULL) {
		ret = ENOMEM;
		krb5_set_error_message(context, ret, "malloc: out of memory");
		return ret;
	    }

	    /* check if already tried */
	    for (e = registered; e != NULL; e = e->next)
		if (e->type == DSO && strcmp(e->u.dso.path, path) == 0)
		    break;
	    if (e) {
		free(path);
	    } else {
		loadlib(context, path); /* store or frees path */
	    }
	}
	closedir(d);
    }
    if (dirs != rk_UNCONST(sysplugin_dirs))
	krb5_config_free_strings(dirs);
#endif /* HAVE_DLOPEN */
    return 0;
}

static krb5_error_code
add_symbol(krb5_context context, struct krb5_plugin **list, void *symbol)
{
    struct krb5_plugin *e;
    
    e = calloc(1, sizeof(*e));
    if (e == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }
    e->symbol = symbol;
    e->next = *list;
    *list = e;
    return 0;
}

krb5_error_code
_krb5_plugin_find(krb5_context context,
		  enum krb5_plugin_type type,
		  const char *name,
		  struct krb5_plugin **list)
{
    struct plugin *e;
    krb5_error_code ret;

    *list = NULL;

    HEIMDAL_MUTEX_lock(&plugin_mutex);
    
    load_plugins(context);

    for (ret = 0, e = registered; e != NULL; e = e->next) {
	switch(e->type) {
	case DSO: {
	    void *sym;
	    if (e->u.dso.dsohandle == NULL)
		continue;
	    sym = dlsym(e->u.dso.dsohandle, name);
	    if (sym)
		ret = add_symbol(context, list, sym);
	    break;
	}
	case SYMBOL:
	    if (strcmp(e->u.symbol.name, name) == 0 && e->u.symbol.type == type)
		ret = add_symbol(context, list, e->u.symbol.symbol);
	    break;
	}
	if (ret) {
	    _krb5_plugin_free(*list);
	    *list = NULL;
	}
    }

    HEIMDAL_MUTEX_unlock(&plugin_mutex);
    if (ret)
	return ret;

    if (*list == NULL) {
	krb5_set_error_message(context, ENOENT, "Did not find a plugin for %s", name);
	return ENOENT;
    }

    return 0;
}

void
_krb5_plugin_free(struct krb5_plugin *list)
{
    struct krb5_plugin *next;
    while (list) {
	next = list->next;
	free(list);
	list = next;
    }
}

/*
 * module - dict of {
 *      ModuleName = [
 *          plugin = object{ 
 *              array = { ptr, ctx }
 *          }
 *      ]
 * }
 */

static heim_dict_t modules;

struct plugin2 {
    heim_string_t path;
    void *dsohandle;
    heim_dict_t names;
};

static void
plug_dealloc(void *ptr)
{
    struct plugin2 *p = ptr;
    heim_release(p->path);
    heim_release(p->names);
    if (p->dsohandle)
	dlclose(p->dsohandle);
}


void
_krb5_load_plugins(krb5_context context, const char *name, const char **paths)
{
#ifdef HAVE_DLOPEN
    heim_string_t s = heim_string_create(name);
    heim_dict_t module;
    struct dirent *entry;
    krb5_error_code ret;
    const char **di;
    DIR *d;

    HEIMDAL_MUTEX_lock(&plugin_mutex);

    if (modules == NULL) {
	modules = heim_dict_create(11);
	if (modules == NULL) {
	    HEIMDAL_MUTEX_unlock(&plugin_mutex);
	    return;
	}
    }

    module = heim_dict_copy_value(modules, s);
    if (module == NULL) {
	module = heim_dict_create(11);
	if (module == NULL) {
	    HEIMDAL_MUTEX_unlock(&plugin_mutex);
	    heim_release(s);
	    return;
	}
	heim_dict_add_value(modules, s, module);
    }
    heim_release(s);

    for (di = paths; *di != NULL; di++) {
	d = opendir(*di);
	if (d == NULL)
	    continue;
	rk_cloexec_dir(d);

	while ((entry = readdir(d)) != NULL) {
	    char *n = entry->d_name;
	    char *path = NULL;
	    heim_string_t spath;
	    struct plugin2 *p;

	    /* skip . and .. */
	    if (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')))
		continue;

	    ret = 0;
#ifdef __APPLE__
	    { /* support loading bundles on MacOS */
		size_t len = strlen(n);
		if (len > 7 && strcmp(&n[len - 7],  ".bundle") == 0)
		    ret = asprintf(&path, "%s/%s/Contents/MacOS/%.*s", *di, n, (int)(len - 7), n);
	    }
#endif
	    if (ret < 0 || path == NULL)
		ret = asprintf(&path, "%s/%s", *di, n);

	    if (ret < 0 || path == NULL)
		continue;

	    spath = heim_string_create(n);
	    if (spath == NULL) {
		free(path);
		continue;
	    }

	    /* check if already cached */
	    p = heim_dict_copy_value(module, spath);
	    if (p == NULL) {
		p = heim_alloc(sizeof(*p), "krb5-plugin", plug_dealloc);
		if (p)
		    p->dsohandle = dlopen(path, RTLD_LOCAL|RTLD_LAZY);

		if (p && p->dsohandle) {
		    p->path = heim_retain(spath);
		    p->names = heim_dict_create(11);
		    heim_dict_add_value(module, spath, p);
		}
	    }
	    heim_release(spath);
	    heim_release(p);
	    free(path);
	}
	closedir(d);
    }
    heim_release(module);
    HEIMDAL_MUTEX_unlock(&plugin_mutex);
#endif /* HAVE_DLOPEN */
}

void
_krb5_unload_plugins(krb5_context context, const char *name)
{
    HEIMDAL_MUTEX_lock(&plugin_mutex);
    heim_release(modules);
    modules = NULL;
    HEIMDAL_MUTEX_unlock(&plugin_mutex);
}

/*
 *
 */

struct common_plugin_method {
    int			version;
    krb5_error_code	(*init)(krb5_context, void **);
    void		(*fini)(void *);
};

struct plug {
    void *dataptr;
    void *ctx;
};

static void
plug_free(void *ptr)
{
    struct plug *pl = ptr;
    if (pl->dataptr) {
	struct common_plugin_method *cpm = pl->dataptr;
	cpm->fini(pl->ctx);
    }
}

krb5_error_code
_krb5_plugin_run_f(krb5_context context,
		   const char *module,
		   const char *name,
		   int min_version,
		   int flags,
		   void *userctx,
		   krb5_error_code (*func)(krb5_context, const void *, void *, void *))
{
    heim_string_t m = heim_string_create(module);
    heim_string_t n = heim_string_create(name);
    heim_array_t result;
    heim_dict_t dict;
    __block int ret = 0;

    result = heim_array_create();

    HEIMDAL_MUTEX_lock(&plugin_mutex);

    dict = heim_dict_copy_value(modules, m);
    heim_release(m);
    if (dict == NULL) {
	HEIMDAL_MUTEX_unlock(&plugin_mutex);
	heim_release(n);
	return KRB5_PLUGIN_NO_HANDLE;
    }

    heim_dict_iterate(dict, ^(heim_object_t key, heim_object_t value){
	    struct plugin2 *p = value;
	    struct plug *pl = heim_dict_copy_value(p->names, n);
	    struct common_plugin_method *cpm;

	    if (pl == NULL) {
		/* check for negative cache entry */
		if (p->dsohandle == NULL)
		    return;

		/* Allow to overwrite loading the module */
		if (!krb5_config_get_bool_default(context, NULL, TRUE, "plugin", "load", heim_string_get_utf8(p->path), NULL))
		    return;
		
		/* Load and setup entry */
		pl = heim_alloc(sizeof(*pl), "struct-plug", plug_free);
		
		cpm = pl->dataptr = dlsym(p->dsohandle, name);
		if (cpm) {
		    ret = cpm->init(context, &pl->ctx);
		    if (ret)
			cpm = pl->dataptr = NULL;
		}
		heim_dict_add_value(p->names, n, pl);
	    } else {
		cpm = pl->dataptr;
	    }
	    
	    if (cpm && cpm->version >= min_version)
		heim_array_append_value(result, pl);
	    
	    heim_release(pl);
	});
    
    heim_release(dict);
    
    HEIMDAL_MUTEX_unlock(&plugin_mutex);
    
    ret = KRB5_PLUGIN_NO_HANDLE;

    heim_array_iterate(result, ^(heim_object_t value, int *stop) {
	    struct plug *pl = value;

	    ret = func(context, pl->dataptr, pl->ctx, userctx);
	    if (ret == 0 || ret != KRB5_PLUGIN_NO_HANDLE)
		*stop = 1;
	});

    heim_release(result);
    heim_release(n);

    return ret;
}
