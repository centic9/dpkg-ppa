/*
 * libdpkg - Debian packaging suite library routines
 * pkg-spec.c - routines to handle package specifiers
 *
 * Copyright © 2011 Linaro Limited
 * Copyright © 2011 Raphaël Hertzog <hertzog@debian.org>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <compat.h>

#include <stdlib.h>
#include <fnmatch.h>
#include <string.h>
#include <assert.h>

#include <dpkg/i18n.h>
#include <dpkg/dpkg.h>
#include <dpkg/dpkg-db.h>
#include <dpkg/arch.h>
#include <dpkg/pkg-spec.h>

static void pkg_spec_iter_reset(struct pkg_spec *ps);
static bool pkg_spec_do_checks(struct pkg_spec *ps, bool fail_on_error);
static bool pkg_spec_match_flags(struct pkg_spec *ps, struct pkginfo *pkg);
static bool pkg_spec_match_arch(struct pkg_spec *ps, const struct dpkg_arch *arch);
static bool pkg_spec_match_pkgname(struct pkg_spec *ps, const char *name);

void
pkg_spec_reset(struct pkg_spec *ps)
{
	if (!(ps->flags & psf_no_copy))
		free(ps->name);
	ps->name = NULL;
	ps->arch = NULL;
	pkg_spec_iter_reset(ps);
	/* flags is left untouched on purpose */
}

bool
pkg_spec_parse(struct pkg_spec *ps, const char *string)
{
	char *arch;

	pkg_spec_reset(ps);

	if (ps->flags & psf_no_copy)
		ps->name = (char *) string;
	else
		ps->name = m_strdup(string);

	arch = strchr(ps->name, ':');
	if (arch) {
		arch[0] = '\0';
		arch++;
	}
	ps->arch = dpkg_arch_find(arch);

	return pkg_spec_do_checks(ps, !(ps->flags & psf_no_check));
}

bool
pkg_spec_do_checks(struct pkg_spec *ps, bool fail_on_error)
{
	const char *emsg;

	ps->name_is_pattern = false;
	ps->arch_is_pattern = false;

	/* Detect if we have patterns and/or illegal names */
	if ((ps->flags & psf_patterns) && strpbrk(ps->name, "*[?\\"))
		ps->name_is_pattern = true;

	if ((ps->flags & psf_patterns) && strpbrk(ps->arch->name, "*[?\\"))
		ps->arch_is_pattern = true;

	emsg = pkg_spec_is_illegal(ps);
	if (fail_on_error && emsg)
		ohshit("%s", emsg);

	return (emsg == NULL);
}

bool
pkg_spec_is_pattern(struct pkg_spec *ps)
{
	if (ps->name_is_pattern || ps->arch_is_pattern)
		return true;
	if ((ps->flags & psf_def_wildcard) && ps->arch->type == arch_none)
		return true;
	return false;
}

const char *
pkg_spec_is_illegal(struct pkg_spec *ps)
{
	static char msg[1024];
	const char *emsg;

	if (!ps->name_is_pattern &&
	    (emsg = pkg_name_is_illegal(ps->name, NULL))) {
		snprintf(msg, 1024, _("package name in specifier '%s%s%s' is "
		                      "illegal: %s"), ps->name,
		         (ps->arch->type != arch_none) ? ":" : "",
		         ps->arch->name, emsg);
		return msg;
	}

	if (!ps->arch_is_pattern && ps->arch->type == arch_illegal) {
		emsg = dpkg_arch_name_is_illegal(ps->arch->name);
		snprintf(msg, 1024, _("architecture name in specifier '%s%s%s' "
		                      "is illegal: %s"), ps->name,
		         (ps->arch->type != arch_none) ? ":" : "",
		         ps->arch->name, emsg);
		return msg;
	}

	return NULL;
}

struct pkginfo *
pkg_spec_find_pkg(struct pkg_spec *ps, const char *string)
{
	struct pkginfo *pkg = NULL;
	const char *emsg;

	if (ps->flags & (psf_patterns | psf_def_wildcard))
		internerr("pkg_spec_find_pkg() incompatible with patterns");

	if (string)
		pkg_spec_parse(ps, string);

	emsg = pkg_spec_is_illegal(ps);
	if (emsg)
		ohshit("%s", emsg);

	pkg = pkg_db_find_pkg(ps->name, ps->arch);
	if (!pkg_spec_match_flags(ps, pkg))
		pkg = NULL;

	if (string)
		pkg_spec_reset(ps);

	return pkg;
}

static bool
pkg_spec_match_arch(struct pkg_spec *ps, const struct dpkg_arch *arch)
{
	if (ps->arch_is_pattern)
		return (fnmatch(ps->arch->name, arch->name, 0) == 0);
	else if (ps->arch->type != arch_none) /* !arch_is_pattern */
		return (ps->arch == arch);

	/* No arch specified  */
	switch (ps->flags & psf_def_mask) {
	case psf_def_native:
		return (arch->type == arch_native || arch->type == arch_all ||
		        arch->type == arch_none);
	case psf_def_wildcard:
		return true;
	default:
		internerr("multiple/unknown/no psf_def_* configured in pkg_spec");
	}
}

static bool
pkg_spec_match_pkgname(struct pkg_spec *ps, const char *name)
{
	if (ps->name_is_pattern) {
		return (fnmatch(ps->name, name, 0) == 0);
	} else {
		return (strcmp(ps->name, name) == 0);
	}
}

static bool
pkg_spec_match_flags(struct pkg_spec *ps, struct pkginfo *pkg)
{
	if ((ps->flags & psf_skip_not_installed) &&
	    pkg->status == stat_notinstalled)
		return false;
	if ((ps->flags & psf_skip_config_files) && pkg &&
	    pkg->status == stat_configfiles)
		return false;
	return true;
}

bool
pkg_spec_match_pkg(struct pkg_spec *ps, struct pkginfo *pkg,
                   struct pkgbin *pbin)
{
	return (pkg_spec_match_flags(ps, pkg) &&
	        pkg_spec_match_arch(ps, pbin->arch) &&
	        pkg_spec_match_pkgname(ps, pkg->set->name));
}

static void
pkg_spec_iter_reset(struct pkg_spec *ps)
{
	if (ps->pkg_iter) {
		pkg_db_iter_free(ps->pkg_iter);
		ps->pkg_iter = NULL;
	}
	ps->pkg_next = NULL;
}

void
pkg_spec_iter_start(struct pkg_spec *ps)
{
	/* Cleanup from previous run if needed */
	pkg_spec_iter_reset(ps);
	/* Initialize iteration variables */
	if (ps->name_is_pattern) {
		ps->pkg_iter = pkg_db_iter_new();
	} else {
		ps->pkg_next = &pkg_db_find_set(ps->name)->pkg;
	}
}

struct pkginfo *
pkg_spec_iter_next_pkg(struct pkg_spec *ps)
{
	struct pkginfo *r;
	struct pkgset *set;

loop_arch:
	/* Loop over all architectures and return the matching ones */
	while (ps->pkg_next) {
		r = ps->pkg_next;
		ps->pkg_next = r->arch_next;
		if (pkg_spec_match_flags(ps, r) &&
		    pkg_spec_match_arch(ps, r->installed.arch))
			return r;
	}

	/* Iterate over other package sets when needed */
	if (!ps->pkg_iter)
		return NULL;

	while ((set = pkg_db_iter_next_set(ps->pkg_iter))) {
		if (!pkg_spec_match_pkgname(ps, set->name))
			continue;
		ps->pkg_next = &set->pkg;
		goto loop_arch;
	}
	return NULL;
}
