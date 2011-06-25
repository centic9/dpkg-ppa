/*
 * dpkg - main program for package management
 * infodb.c - package control information database
 *
 * Copyright © 1995 Ian Jackson <ian@chiark.greenend.org.uk>
 * Copyright © 2011 Guillem Jover <guillem@debian.org>
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

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include <dpkg/i18n.h>
#include <dpkg/dpkg.h>
#include <dpkg/dpkg-db.h>
#include <dpkg/dir.h>
#include <dpkg/fdio.h>
#include <dpkg/debug.h>
#include <dpkg/varbuf.h>

#include "filesdb.h"
#include "infodb.h"

/* Forward declarations of static functions */
static void pkg_infodb_upgrade_to_multiarch(void);

/* Global variables */
static int db_format;
static char *db_format_file;

void
pkg_infodb_init(const enum modstatdb_rw flags)
{
	int fd;

	if (db_format_file)
		free(db_format_file);
	db_format_file = dpkg_db_get_path("format");
	fd = open(db_format_file, O_RDONLY);
	if (fd < 0 && errno == ENOENT) {
		db_format = 0; /* Lack of file means old format */
	} else if (fd < 0) {
		ohshite(_("error trying to open %.250s"), db_format_file);
	} else {
		char format[16], *endptr = NULL;
		ssize_t size;

		size = fd_read(fd, format, sizeof(format) - 1);
		if (size < 0) {
			ohshite(_("error while reading %s"), db_format_file);
		}
		format[size] = '\0';
		db_format = strtol(format, &endptr, 10);
		if (endptr && *endptr != '\0' && *endptr != '\n')
			ohshit(_("%s is corrupted, it should contain the "
			         "database format version (an integer)"),
			       db_format_file);
		close(fd);
	}
	if (flags >= msdbrw_write && db_format < 2)
		pkg_infodb_upgrade_to_multiarch();
}

int
pkg_infodb_format(void)
{
	return db_format;
}

struct match_node {
	struct match_node *next;
	char *old;
	char *new;
};

static struct match_node *match_head = NULL;

static struct match_node *
match_node_new(const char *old, const char *new, struct match_node *next)
{
	struct match_node *node;

	node = m_malloc(sizeof(*node));
	node->next = next;
	node->old = m_strdup(old);
	node->new = m_strdup(new);

	return node;
}

static void
match_node_free(struct match_node *node)
{
	free(node->old);
	free(node->new);
	free(node);
}

static void
pkg_infodb_setup_multiarch_path(const char *filename, const char *filetype)
{
	static struct varbuf pkgname;
	const char *name, *dot;
	struct pkginfo *pkg;
	struct pkgset *set;

	dot = strrchr(filename, '.');
	name = strrchr(filename, '/');
	if (name == NULL)
		name = filename;
	else
		name++;

	varbuf_reset(&pkgname);
	varbuf_add_buf(&pkgname, name, dot - name);
	varbuf_end_str(&pkgname);

	if (strchr(pkgname.buf, ':'))
		return; /* Skip files already converted */

	set = pkg_db_find_set(pkgname.buf);
	pkg = &set->pkg;
	while (pkg) {
		if (pkg->status != stat_notinstalled)
			break;
		pkg = pkg->arch_next;
	}
	if (!pkg) {
		warning(_("Info file %s not associated to any package"), filename);
		return;
	}
	if (pkg->installed.multiarch == multiarch_same) {
		/* We found one to ugprade */
		struct varbuf new = VARBUF_INIT;
		struct stat st;

		varbuf_add_str(&new, pkgadmindir());
		varbuf_pkg(&new, pkg, pdo_always);
		varbuf_add_char(&new, '.');
		varbuf_add_str(&new, filetype);
		varbuf_end_str(&new);
		if (stat(new.buf, &st) && errno == ENOENT) {
			if (link(filename, new.buf))
				ohshite(_("error creating hard link `%.255s'"),
				        new.buf);
		}
		match_head = match_node_new(filename, new.buf, match_head);
		varbuf_destroy(&new);
	}
}

static void
pkg_infodb_record_format(int version)
{
	int fd, size;
	char format[16];
	ssize_t written;

	size = snprintf(format, sizeof(format), "%d", version);
	fd = open(db_format_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 1)
		ohshite(_("unable to open/create '%s'"), db_format_file);
	written = fd_write(fd, format, size);
	if (written < 0) {
		ohshite(_("error while writing '%s'"), db_format_file);
	}
	if (fsync(fd))
		ohshite(_("unable to sync file '%s'"), db_format_file);
	if (close(fd))
		ohshite(_("unable to close file '%s'"), db_format_file);
	dir_sync_path_parent(db_format_file);
	db_format = version;
}

static void
cu_abort_db_upgrade(int argc, void **argv)
{
	struct match_node *next;
	struct stat st;

	/* Restore the old files if needed and drop the newly created files */
	while (match_head) {
		next = match_head->next;
		if (stat(match_head->old, &st) && errno == ENOENT)
			if (link(match_head->new, match_head->old))
				ohshite(_("error creating hard link `%.255s'"),
				        match_head->old);
		if (unlink(match_head->new))
			ohshite(_("cannot remove `%.250s'"), match_head->new);
		match_node_free(match_head);
		match_head = next;
	}
	pkg_infodb_record_format(0);
}

static void
pkg_infodb_upgrade_to_multiarch(void)
{
	struct match_node *next;

	push_cleanup(cu_abort_db_upgrade, ehflag_bombout, NULL, 0, 0);
	pkg_infodb_foreach(NULL, NULL, pkg_infodb_setup_multiarch_path);
	pkg_infodb_record_format(1);
	while (match_head) {
		next = match_head->next;
		if (unlink(match_head->old))
			ohshite(_("cannot remove `%.250s'"), match_head->old);
		match_node_free(match_head);
		match_head = next;
	}
	pkg_infodb_record_format(2);
	pop_cleanup(ehflag_normaltidy);
}

bool
pkg_infodb_has_file(struct pkginfo *pkg, struct pkgbin *pkgbin,
                    const char *name)
{
	const char *filename;
	struct stat stab;

	filename = pkgadminfile(pkg, pkgbin, name);
	if (lstat(filename, &stab) == 0)
		return true;
	else if (errno == ENOENT)
		return false;
	else
		ohshite(_("unable to check existence of `%.250s'"), filename);
}

void
pkg_infodb_foreach(struct pkginfo *pkg, struct pkgbin *pkgbin,
                   pkg_infodb_file_func *func)
{
	DIR *db_dir;
	struct dirent *db_de;
	struct varbuf db_path = VARBUF_INIT;
	struct varbuf pkgname = VARBUF_INIT;
	size_t db_path_len;

	if (pkg) {
		varbuf_add_str(&pkgname, pkg->set->name);
		if (pkgbin->multiarch == multiarch_same &&
		    pkg_infodb_format() > 0) {
			varbuf_add_char(&pkgname, ':');
			varbuf_add_str(&pkgname, pkgbin->arch->name);
		}
		varbuf_end_str(&pkgname);
	}

	varbuf_add_str(&db_path, pkgadmindir());
	db_path_len = db_path.used;
	varbuf_add_char(&db_path, '\0');

	db_dir = opendir(db_path.buf);
	if (!db_dir)
		ohshite(_("cannot read info directory"));

	push_cleanup(cu_closedir, ~0, NULL, 0, 1, (void *)db_dir);
	while ((db_de = readdir(db_dir)) != NULL) {
		const char *filename, *filetype, *dot;

		debug(dbg_veryverbose, "infodb foreach info file '%s'",
		      db_de->d_name);

		/* Ignore dotfiles, including ‘.’ and ‘..’. */
		if (db_de->d_name[0] == '.')
			continue;

		/* Ignore anything odd. */
		dot = strrchr(db_de->d_name, '.');
		if (dot == NULL)
			continue;

		/* Ignore files from other packages if pkg is supplied. */
		if (pkg) {
			if (pkgname.used != (size_t)(dot - db_de->d_name) ||
		            strncmp(db_de->d_name, pkgname.buf, pkgname.used))
				continue;
			debug(dbg_stupidlyverbose,
			      "infodb foreach file this pkg");
		}

		/* Skip past the full stop. */
		filetype = dot + 1;

		varbuf_trunc(&db_path, db_path_len);
		varbuf_add_str(&db_path, db_de->d_name);
		varbuf_end_str(&db_path);
		filename = db_path.buf;

		func(filename, filetype);
	}
	pop_cleanup(ehflag_normaltidy); /* closedir */

	varbuf_destroy(&db_path);
	varbuf_destroy(&pkgname);
}
