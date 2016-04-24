/*
 * libdpkg - Debian packaging suite library routines
 * arch.h - architecture database functions
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

#ifndef LIBDPKG_ARCH_H
#define LIBDPKG_ARCH_H

#include <dpkg/macros.h>

DPKG_BEGIN_DECLS

struct dpkg_arch {
	struct dpkg_arch *next;
	const char *name;
	enum arch_type {
		arch_all,
		arch_native,
		arch_foreign,
		arch_wildcard,
		arch_none,
		arch_illegal,
		arch_unknown,
	} type;
};

struct dpkg_arch *dpkg_arch_find(const char *name);
struct dpkg_arch *dpkg_arch_get_native(void);
struct dpkg_arch *dpkg_arch_get_list(void);
const char *dpkg_arch_name_is_illegal(const char *name) DPKG_ATTR_NONNULL(1);
void dpkg_arch_reset(void);

DPKG_END_DECLS

#endif /* LIBDPKG_ARCH_H */
