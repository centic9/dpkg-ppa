/*
 * libdpkg - Debian packaging suite library routines
 * pkg-spec.h - definitions for package specifiers handling
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

#ifndef DPKG_PKGSPEC_H
#define DPKG_PKGSPEC_H

#include <stdbool.h>

#include <dpkg/macros.h>
#include <dpkg/dpkg-db.h>
#include <dpkg/varbuf.h>
#include <dpkg/arch.h>

DPKG_BEGIN_DECLS

struct pkg_spec {
	char *name;
	const struct dpkg_arch *arch;

	enum pkg_spec_flags {
		psf_no_copy            = 00001, /* No need to strdup name */
		psf_no_check           = 00002, /* Don't fail on illegal pkg/arch names */
		psf_patterns           = 00004, /* Detect patterns */
		psf_skip_not_installed = 00010, /* Skip stat_notinstalled */
		psf_skip_config_files  = 00020, /* Skip stat_configfiles */

		/* How to consider the lack of an arch qualifier */
		psf_def_native         = 00100,
		psf_def_wildcard       = 00200,
		psf_def_mask           = 00300,
	} flags;

	/* Members below are private */
	bool name_is_pattern;
	bool arch_is_pattern;

	/* Used for the iterator */
	struct pkgiterator *pkg_iter;
	struct pkginfo *pkg_next;
};

#define PKG_SPEC_INIT(flags) { NULL, NULL, flags, false, false, NULL, NULL }

void pkg_spec_reset(struct pkg_spec *ps);
bool pkg_spec_parse(struct pkg_spec *ps, const char *string);

struct pkginfo *pkg_spec_find_pkg(struct pkg_spec *ps, const char* string);
bool pkg_spec_match_pkg(struct pkg_spec *ps, struct pkginfo *pkg,
                        struct pkgbin *pbin);

bool pkg_spec_is_pattern(struct pkg_spec *ps);
const char *pkg_spec_is_illegal(struct pkg_spec *ps);

void pkg_spec_iter_start(struct pkg_spec *ps);
struct pkginfo *pkg_spec_iter_next_pkg(struct pkg_spec *ps);

DPKG_END_DECLS

#endif
