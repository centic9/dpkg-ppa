/*
 * libdpkg - Debian packaging suite library routines
 * pkg-db.c - Low level package database routines (hash tables, etc.)
 *
 * Copyright © 1995 Ian Jackson <ian@chiark.greenend.org.uk>
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

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include <dpkg/i18n.h>
#include <dpkg/dpkg.h>
#include <dpkg/dpkg-db.h>
#include <dpkg/arch.h>

/* This must always be a prime for optimal performance.
 * With 4093 buckets, we glean a 20% speedup, for 8191 buckets
 * we get 23%. The nominal increase in memory usage is a mere
 * sizeof(void *) * 8063 (i.e. less than 32 KiB on 32bit systems). */
#define BINS 8191

static struct pkgset *bins[BINS];
static int npkg, nset;

#define FNV_offset_basis 2166136261ul
#define FNV_mixing_prime 16777619ul

/**
 * Fowler/Noll/Vo -- simple string hash.
 *
 * For more info, see <http://www.isthe.com/chongo/tech/comp/fnv/index.html>.
 */
static unsigned int hash(const char *name) {
  register unsigned int h = FNV_offset_basis;
  register unsigned int p = FNV_mixing_prime;
  while( *name ) {
    h *= p;
    h ^= *name++;
  }
  return h;
}

/**
 * Return the package set with the given name.
 *
 * If the package already exists in the internal database, then it returns
 * the existing structure. Otherwise it allocates a new one and will
 * return it. The actual name associated to the package set is a lowercase
 * version of the name given in parameter.
 *
 * A package set (struct pkgset) can be composed of multiple package
 * instances (struct pkginfo) where each instance is distinguished
 * by its architecture (as recorded in pkg.{installed,available}.architecture).
 *
 * @param inname Name of the package set.
 * @return The package set.
 */
struct pkgset *
pkg_db_find_set(const char *inname)
{
  struct pkgset **pointerp, *newpkg;
  char *name = m_strdup(inname), *p;

  p= name;
  while(*p) { *p= tolower(*p); p++; }

  pointerp= bins + (hash(name) % (BINS));
  while (*pointerp && strcasecmp((*pointerp)->name,name))
    pointerp= &(*pointerp)->next;
  if (*pointerp) {
    free(name);
    return *pointerp;
  }

  newpkg = nfmalloc(sizeof(struct pkgset));
  pkgset_blank(newpkg);
  newpkg->name= nfstrsave(name);
  newpkg->next= NULL;
  *pointerp= newpkg;
  nset++;
  npkg++;

  free(name);
  return newpkg;
}

/**
 * Return the package instance with the given name and architecture.
 *
 * It first uses pkg_db_find_set() to retrieve the right package set and
 * then traverse the various instances to find out whether there's one
 * matching the given architecture. If yes, it returns it. Otherwise it
 * allocates an new instance and register it in the package set berfore
 * returning it.
 *
 * If the requested architecture is a NULL pointer, it will be dealt like
 * the native architecture and the special architecture "all": it will
 * always return the first instance in the list (this one always exists
 * since it's part of the pksget structure).
 *
 * @param name The package name.
 * @param arch The requested architecture.
 * @return The package instance.
 */
struct pkginfo *
pkg_db_find_pkg(const char *name, const struct dpkg_arch *arch)
{
  struct pkgset *set;
  struct pkginfo *pkg, **pkgp;

  set = pkg_db_find_set(name);
  pkg = &set->pkg;
  if (!arch || arch->type == arch_native || arch->type == arch_all ||
      arch->type == arch_none) {
    /* Must always use the first instance */
    assert(pkg->installed.arch->type == arch_native ||
           pkg->installed.arch->type == arch_all ||
           pkg->installed.arch->type == arch_none);
    return pkg;
  }

  /* Look into alternate instances for the wanted architecture */
  pkgp = &pkg->arch_next;
  while (*pkgp) {
    if (!(*pkgp)->installed.arch) {
      /* pkginfo is not differentiated, use it */
      (*pkgp)->installed.arch = arch;
      (*pkgp)->available.arch = arch;
      return *pkgp;
    }
    if ((*pkgp)->installed.arch == arch)
      return *pkgp;
    pkgp = &(*pkgp)->arch_next;
  }

  /* Need to create a new instance for the wanted architecture */
  pkg = nfmalloc(sizeof(struct pkginfo));
  pkg_blank(pkg);
  pkg->set = set;
  pkg->installed.arch = arch;
  pkg->available.arch = arch;
  *pkgp = pkg;
  npkg++;

  return pkg;
}

/**
 * Return the number of package sets available in the database.
 *
 * @return The number of package sets.
 */
int
pkg_db_count_set(void)
{
  return nset;
}

/**
 * Return the number of package instances available in the database.
 *
 * @return The number of package instances.
 */
int
pkg_db_count_pkg(void)
{
  return npkg;
}

struct pkgiterator {
  struct pkginfo *pigp;
  int nbinn;
};

/**
 * Create a new package iterator.
 *
 * It can iterate either over package sets or over package instances.
 *
 * @return The iterator.
 */
struct pkgiterator *
pkg_db_iter_new(void)
{
  struct pkgiterator *i;
  i= m_malloc(sizeof(struct pkgiterator));
  i->pigp= NULL;
  i->nbinn= 0;
  return i;
}

/**
 * Return the next package set in the database.
 *
 * If no further package set is available, it will return NULL.
 *
 * @name it The iterator.
 * @return A package set.
 */
struct pkgset *
pkg_db_iter_next_set(struct pkgiterator *it)
{
  struct pkgset *set;

  while (!it->pigp) {
    if (it->nbinn >= BINS)
      return NULL;
    if (bins[it->nbinn])
      it->pigp = &bins[it->nbinn]->pkg;
    it->nbinn++;
  }

  set = it->pigp->set;
  if (set->next)
    it->pigp = &set->next->pkg;
  else
    it->pigp = NULL;

  return set;
}

/**
 * Return the next package instance in the database.
 *
 * If no further package instance is available, it will return NULL. Note
 * that it will return all instances of a given package set in sequential
 * order. The first instance for a given package set will always
 * correspond to the native architecture even if that package is not
 * installed or available.
 *
 * @name i The iterator.
 * @return A package instance.
 */
struct pkginfo *
pkg_db_iter_next_pkg(struct pkgiterator *i)
{
  struct pkginfo *r;

  while (!i->pigp) {
    if (i->nbinn >= BINS) return NULL;
    if (bins[i->nbinn])
      i->pigp = &bins[i->nbinn]->pkg;
    i->nbinn++;
  }

  r = i->pigp;
  if (r->arch_next)
    i->pigp = r->arch_next;
  else if (r->set->next)
    i->pigp = &r->set->next->pkg;
  else
    i->pigp = NULL;

  return r;
}

/**
 * Free up the resources associated to the package iterator.
 *
 * @name i The iterator.
 */
void
pkg_db_iter_free(struct pkgiterator *i)
{
  free(i);
}

void
pkg_db_reset(void)
{
  int i;
  nffreeall();
  nset = 0;
  npkg = 0;
  for (i=0; i<BINS; i++) bins[i]= NULL;
  dpkg_arch_reset();
}

void
pkg_db_report(FILE *file)
{
  int i, c;
  struct pkgset *pkg;
  int *freq;

  freq = m_malloc(sizeof(int) * nset + 1);
  for (i = 0; i <= nset; i++) freq[i]= 0;
  for (i=0; i<BINS; i++) {
    for (c=0, pkg= bins[i]; pkg; c++, pkg= pkg->next);
    fprintf(file,"bin %5d has %7d\n",i,c);
    freq[c]++;
  }
  for (i = nset; i > 0 && freq[i] == 0; i--);
  while (i >= 0) {
    fprintf(file, "size %7d occurs %5d times\n", i, freq[i]);
    i--;
  }

  m_output(file, "<hash report>");

  free(freq);
}
