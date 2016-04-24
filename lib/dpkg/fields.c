/*
 * libdpkg - Debian packaging suite library routines
 * fields.c - parsing of all the different fields, when reading in
 *
 * Copyright © 1995 Ian Jackson <ian@chiark.greenend.org.uk>
 * Copyright © 2001 Wichert Akkerman
 * Copyright © 2009 Canonical Ltd.
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

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include <dpkg/i18n.h>
#include <dpkg/dpkg.h>
#include <dpkg/dpkg-db.h>
#include <dpkg/arch.h>
#include <dpkg/path.h>
#include <dpkg/parsedump.h>
#include <dpkg/pkg-spec.h>
#include <dpkg/triglib.h>

static int
parse_nv_next(struct parsedb_state *ps, const struct pkginfo *pigp,
              const char *what, const struct namevalue *nv_head,
              const char **strp)
{
  const char *str_start = *strp, *str_end;
  const struct namevalue *nv;

  if (str_start[0] == '\0')
    parse_error(ps, pigp, _("%s is missing"), what);

  nv = namevalue_find_by_name(nv_head, str_start);
  if (nv == NULL)
    parse_error(ps, pigp, _("'%.50s' is not allowed for %s"), str_start, what);

  /* We got the fallback value, skip further string validation. */
  if (nv->length == 0) {
    str_end = NULL;
  } else {
    str_end = str_start + nv->length;
    while (isspace(str_end[0]))
      str_end++;
  }
  *strp = str_end;

  return nv->value;
}

static int
parse_nv_last(struct parsedb_state *ps, const struct pkginfo *pkg,
              const char *what, const struct namevalue *nv_head,
              const char *str)
{
  int value;

  value = parse_nv_next(ps, pkg, what, nv_head, &str);
  if (str != NULL && str[0] != '\0')
    parse_error(ps, pkg, _("junk after %s"), what);

  return value;
}

void
f_name(struct pkginfo *pigp, struct pkgbin *pifp,
       struct parsedb_state *ps,
       const char *value, const struct fieldinfo *fip)
{
  const char *e;

  e = pkg_name_is_illegal(value, NULL);
  if (e != NULL)
    parse_error(ps, pigp, _("invalid package name (%.250s)"), e);
  /* We use the new name, as pkg_db_find_set() may have done a tolower for us. */
  pigp->set->name = pkg_db_find_set(value)->name;
}

void
f_filecharf(struct pkginfo *pigp, struct pkgbin *pifp,
            struct parsedb_state *ps,
            const char *value, const struct fieldinfo *fip)
{
  struct filedetails *fdp, **fdpp;
  char *cpos, *space;
  int allowextend;

  if (!*value)
    parse_error(ps, pigp, _("empty file details field `%s'"), fip->name);
  if (!(ps->flags & pdb_recordavailable))
    parse_error(ps, pigp,
                _("file details field `%s' not allowed in status file"),
               fip->name);
  allowextend= !pigp->files;
  fdpp= &pigp->files;
  cpos= nfstrsave(value);
  while (*cpos) {
    space= cpos; while (*space && !isspace(*space)) space++;
    if (*space)
      *space++ = '\0';
    fdp= *fdpp;
    if (!fdp) {
      if (!allowextend)
        parse_error(ps, pigp,
                    _("too many values in file details field `%s' "
                      "(compared to others)"), fip->name);
      fdp= nfmalloc(sizeof(struct filedetails));
      fdp->next= NULL;
      fdp->name= fdp->msdosname= fdp->size= fdp->md5sum= NULL;
      *fdpp= fdp;
    }
    FILEFFIELD(fdp,fip->integer,const char*)= cpos;
    fdpp= &fdp->next;
    while (*space && isspace(*space)) space++;
    cpos= space;
  }
  if (*fdpp)
    parse_error(ps, pigp,
                _("too few values in file details field `%s' "
                  "(compared to others)"), fip->name);
}

void
f_charfield(struct pkginfo *pigp, struct pkgbin *pifp,
            struct parsedb_state *ps,
            const char *value, const struct fieldinfo *fip)
{
  if (*value) PKGPFIELD(pifp,fip->integer,char*)= nfstrsave(value);
}

void
f_boolean(struct pkginfo *pigp, struct pkgbin *pifp,
          struct parsedb_state *ps,
          const char *value, const struct fieldinfo *fip)
{
  bool boolean;

  if (!*value)
    return;

  boolean = parse_nv_last(ps, pigp, _("yes/no in boolean field"),
                          booleaninfos, value);
  PKGPFIELD(pifp, fip->integer, bool) = boolean;
}

void
f_multiarch(struct pkginfo *pigp, struct pkgbin *pifp,
            struct parsedb_state *ps,
            const char *value, const struct fieldinfo *fip)
{
  int multiarch;

  if (!*value)
    return;

  multiarch = parse_nv_last(ps, pigp,
                            _("foreign/allowed/same/no in quadstate field"),
                            multiarchinfos, value);
  PKGPFIELD(pifp, fip->integer, int) = multiarch;
}

void
f_architecture(struct pkginfo *pigp, struct pkgbin *pifp,
               struct parsedb_state *ps,
               const char *value, const struct fieldinfo *fip)
{
  if (!*value)
    return;

  pifp->arch = dpkg_arch_find(value);
  if (pifp->arch->type == arch_illegal)
    parse_warn(ps, pigp, _("'%s' is not a valid architecture name: %s"),
               value, dpkg_arch_name_is_illegal(value));
}

void
f_section(struct pkginfo *pigp, struct pkgbin *pifp,
          struct parsedb_state *ps,
          const char *value, const struct fieldinfo *fip)
{
  if (!*value) return;
  pigp->section= nfstrsave(value);
}

void
f_priority(struct pkginfo *pigp, struct pkgbin *pifp,
           struct parsedb_state *ps,
           const char *value, const struct fieldinfo *fip)
{
  if (!*value) return;
  pigp->priority = parse_nv_last(ps, pigp, _("word in `priority' field"),
                                 priorityinfos, value);
  if (pigp->priority == pri_other) pigp->otherpriority= nfstrsave(value);
}

void
f_status(struct pkginfo *pigp, struct pkgbin *pifp,
         struct parsedb_state *ps,
         const char *value, const struct fieldinfo *fip)
{
  if (ps->flags & pdb_rejectstatus)
    parse_error(ps, pigp,
                _("value for `status' field not allowed in this context"));
  if (ps->flags & pdb_recordavailable)
    return;

  pigp->want = parse_nv_next(ps, pigp,
                             _("first (want) word in `status' field"),
                             wantinfos, &value);
  pigp->eflag = parse_nv_next(ps, pigp,
                              _("second (error) word in `status' field"),
                              eflaginfos, &value);
  pigp->status = parse_nv_last(ps, pigp,
                               _("third (status) word in `status' field"),
                               statusinfos, value);
}

void
f_version(struct pkginfo *pigp, struct pkgbin *pifp,
          struct parsedb_state *ps,
          const char *value, const struct fieldinfo *fip)
{
  parse_db_version(ps, pigp, &pifp->version, value,
                   _("error in Version string '%.250s'"), value);
}

void
f_revision(struct pkginfo *pigp, struct pkgbin *pifp,
           struct parsedb_state *ps,
           const char *value, const struct fieldinfo *fip)
{
  char *newversion;

  parse_warn(ps, pigp,
             _("obsolete `Revision' or `Package-Revision' field used"));
  if (!*value) return;
  if (pifp->version.revision && *pifp->version.revision) {
    newversion= nfmalloc(strlen(pifp->version.version)+strlen(pifp->version.revision)+2);
    sprintf(newversion,"%s-%s",pifp->version.version,pifp->version.revision);
    pifp->version.version= newversion;
  }
  pifp->version.revision= nfstrsave(value);
}

void
f_configversion(struct pkginfo *pigp, struct pkgbin *pifp,
                struct parsedb_state *ps,
                const char *value, const struct fieldinfo *fip)
{
  if (ps->flags & pdb_rejectstatus)
    parse_error(ps, pigp,
                _("value for `config-version' field not allowed in this context"));
  if (ps->flags & pdb_recordavailable)
    return;

  parse_db_version(ps, pigp, &pigp->configversion, value,
                   _("error in Config-Version string '%.250s'"), value);

}

/*
 * The code in f_conffiles ensures that value[-1] == ' ', which is helpful.
 */
static void conffvalue_lastword(const char *value, const char *from,
                                const char *endent,
                                const char **word_start_r, int *word_len_r,
                                const char **new_from_r,
                                struct parsedb_state *ps,
                                struct pkginfo *pigp)
{
  const char *lastspc;

  if (from <= value+1) goto malformed;
  for (lastspc= from-1; *lastspc != ' '; lastspc--);
  if (lastspc <= value+1 || lastspc >= endent-1) goto malformed;

  *new_from_r= lastspc;
  *word_start_r= lastspc + 1;
  *word_len_r= (int)(from - *word_start_r);
  return;

malformed:
  parse_error(ps, pigp,
              _("value for `conffiles' has malformatted line `%.*s'"),
              (int)min(endent - value, 250), value);
}

void
f_conffiles(struct pkginfo *pigp, struct pkgbin *pifp,
            struct parsedb_state *ps,
            const char *value, const struct fieldinfo *fip)
{
  static const char obsolete_str[]= "obsolete";
  struct conffile **lastp, *newlink;
  const char *endent, *endfn, *hashstart;
  int c, namelen, hashlen, obsolete;
  char *newptr;

  lastp= &pifp->conffiles;
  while (*value) {
    c= *value++;
    if (c == '\n') continue;
    if (c != ' ')
      parse_error(ps, pigp,
                  _("value for `conffiles' has line starting with non-space `%c'"),
                  c);
    for (endent = value; (c = *endent) != '\0' && c != '\n'; endent++) ;
    conffvalue_lastword(value, endent, endent,
			&hashstart, &hashlen, &endfn,
                        ps, pigp);
    obsolete= (hashlen == sizeof(obsolete_str)-1 &&
	       !memcmp(hashstart, obsolete_str, hashlen));
    if (obsolete)
      conffvalue_lastword(value, endfn, endent,
			  &hashstart, &hashlen, &endfn,
			  ps, pigp);
    newlink= nfmalloc(sizeof(struct conffile));
    value = path_skip_slash_dotslash(value);
    namelen= (int)(endfn-value);
    if (namelen <= 0)
      parse_error(ps, pigp,
                  _("root or null directory is listed as a conffile"));
    newptr = nfmalloc(namelen+2);
    newptr[0]= '/';
    memcpy(newptr+1,value,namelen);
    newptr[namelen+1] = '\0';
    newlink->name= newptr;
    newptr= nfmalloc(hashlen+1);
    memcpy(newptr, hashstart, hashlen);
    newptr[hashlen] = '\0';
    newlink->hash= newptr;
    newlink->obsolete= obsolete;
    newlink->next =NULL;
    *lastp= newlink;
    lastp= &newlink->next;
    value= endent;
  }
}

void
f_dependency(struct pkginfo *pigp, struct pkgbin *pifp,
             struct parsedb_state *ps,
             const char *value, const struct fieldinfo *fip)
{
  char c1, c2;
  const char *p, *emsg;
  const char *depnamestart, *versionstart, *archstart;
  int depnamelength, versionlength, archlength;
  static struct varbuf depname, version, arch;

  struct dependency *dyp, **ldypp;
  struct deppossi *dop, **ldopp;

  /* Empty fields are ignored. */
  if (!*value)
    return;
  p= value;
  ldypp= &pifp->depends; while (*ldypp) ldypp= &(*ldypp)->next;

   /* Loop creating new struct dependency's. */
  for (;;) {
    dyp= nfmalloc(sizeof(struct dependency));
    /* Set this to NULL for now, as we don't know what our real
     * struct pkginfo address (in the database) is going to be yet. */
    dyp->up = NULL;
    dyp->next= NULL; *ldypp= dyp; ldypp= &dyp->next;
    dyp->list= NULL; ldopp= &dyp->list;
    dyp->type= fip->integer;

    /* Loop creating new struct deppossi's. */
    for (;;) {
      depnamestart= p;
      /* Skip over package name characters. */
      while (*p && !isspace(*p) && *p != ':' && *p != '(' && *p != ',' &&
             *p != '|')
        p++;
      depnamelength= p - depnamestart ;
      varbuf_reset(&depname);
      varbuf_add_buf(&depname, depnamestart, depnamelength);
      varbuf_end_str(&depname);
      if (!depname.buf[0])
        parse_error(ps, pigp,
                    _("`%s' field, missing package name, or garbage where "
                      "package name expected"), fip->name);
      emsg = pkg_name_is_illegal(depname.buf, NULL);
      if (emsg)
        parse_error(ps, pigp,
                    _("`%s' field, invalid package name `%.255s': %s"),
                    fip->name, depname.buf, emsg);
      dop= nfmalloc(sizeof(struct deppossi));
      dop->up= dyp;
      dop->ed = pkg_db_find_set(depname.buf);
      dop->next= NULL; *ldopp= dop; ldopp= &dop->next;

      /* Don't link this (which is after all only ‘newpig’ from
       * the main parsing loop in parsedb) into the depended on
       * packages' lists yet. This will be done later when we
       * install this (in parse.c). For the moment we do the
       * ‘forward’ links in deppossi (‘ed’) only, and the ‘backward’
       * links from the depended on packages to dop are left undone. */
      dop->rev_next = NULL;
      dop->rev_prev = NULL;

      dop->cyclebreak = false;

      /* See if we have an architecture specifier */
      if (*p == ':') {
        archstart = ++p;
        while (*p && !isspace(*p) && *p != '(' && *p != ',' && *p != '|')
          p++;
        archlength = p - archstart;
        varbuf_reset(&arch);
        varbuf_add_buf(&arch, archstart, archlength);
        varbuf_add_char(&arch, '\0');
        if (!arch.buf[0])
          parse_error(ps, pigp,
                      _("'%s' field, missing architecture name, or garbage "
                        "where architecture name expected"), fip->name);
        dop->arch = dpkg_arch_find(arch.buf);
        dop->arch_is_implicit = false;
        if (strcmp(arch.buf, "any") != 0)
          emsg = _("a value different from 'any' is currently not allowed");
        else if (dop->arch->type == arch_illegal)
          emsg = dpkg_arch_name_is_illegal(arch.buf);
        if (emsg)
          parse_error(ps, pigp,
                      _("'%s' field, reference to '%.255s': "
                        "invalid architecture name '%.255s': %s"),
                      fip->name, depname.buf, arch.buf, emsg);
      } else if (fip->integer == dep_conflicts || fip->integer == dep_breaks ||
                 fip->integer == dep_replaces) {
        /* Conflics/Breaks/Replaces get an implicit "any" arch qualifier. */
        dop->arch = dpkg_arch_find("any");
        dop->arch_is_implicit = true;
      } else {
        dop->arch = NULL;
        dop->arch_is_implicit = true;
      }

      /* Skip whitespace after package name. */
      while (isspace(*p)) p++;

      /* See if we have a versioned relation. */
      if (*p == '(') {
        p++; while (isspace(*p)) p++;
        c1= *p;
        if (c1 == '<' || c1 == '>') {
          c2= *++p;
          dop->verrel= (c1 == '<') ? dvrf_earlier : dvrf_later;
          if (c2 == '=') {
            dop->verrel |= (dvrf_orequal | dvrf_builtup);
            p++;
          } else if (c2 == c1) {
            dop->verrel |= (dvrf_strict | dvrf_builtup);
            p++;
          } else if (c2 == '<' || c2 == '>') {
            parse_error(ps, pigp,
                        _("`%s' field, reference to `%.255s':\n"
                          " bad version relationship %c%c"),
                        fip->name, depname.buf, c1, c2);
            dop->verrel= dvr_none;
          } else {
            parse_warn(ps, pigp,
                       _("`%s' field, reference to `%.255s':\n"
                         " `%c' is obsolete, use `%c=' or `%c%c' instead"),
                       fip->name, depname.buf, c1, c1, c1, c1);
            dop->verrel |= (dvrf_orequal | dvrf_builtup);
          }
        } else if (c1 == '=') {
          dop->verrel= dvr_exact;
          p++;
        } else {
          parse_warn(ps, pigp,
                     _("`%s' field, reference to `%.255s':\n"
                       " implicit exact match on version number, "
                       "suggest using `=' instead"),
                     fip->name, depname.buf);
          dop->verrel= dvr_exact;
        }
	if ((dop->verrel!=dvr_exact) && (fip->integer==dep_provides))
          parse_warn(ps, pigp,
                     _("Only exact versions may be used for Provides"));

        if (!isspace(*p) && !isalnum(*p)) {
          parse_warn(ps, pigp,
                     _("`%s' field, reference to `%.255s':\n"
                       " version value starts with non-alphanumeric, "
                       "suggest adding a space"),
                     fip->name, depname.buf);
        }
        /* Skip spaces between the relation and the version. */
        while (isspace(*p)) p++;

	versionstart= p;
        while (*p && *p != ')' && *p != '(') {
          if (isspace(*p)) break;
          p++;
        }
	versionlength= p - versionstart;
        while (isspace(*p)) p++;
        if (*p == '(')
          parse_error(ps, pigp,
                      _("`%s' field, reference to `%.255s': "
                        "version contains `%c'"), fip->name, depname.buf, ')');
        else if (*p != ')')
          parse_error(ps, pigp,
                      _("`%s' field, reference to `%.255s': "
                        "version contains `%c'"), fip->name, depname.buf, ' ');
        else if (*p == '\0')
          parse_error(ps, pigp,
                      _("`%s' field, reference to `%.255s': "
                        "version unterminated"), fip->name, depname.buf);
        varbuf_reset(&version);
        varbuf_add_buf(&version, versionstart, versionlength);
        varbuf_end_str(&version);
        parse_db_version(ps, pigp, &dop->version, version.buf,
                         _("'%s' field, reference to '%.255s': "
                           "error in version"), fip->name, depname.buf);
        p++; while (isspace(*p)) p++;
      } else {
        dop->verrel= dvr_none;
        blankversion(&dop->version);
      }
      if (!*p || *p == ',') break;
      if (*p != '|')
        parse_error(ps, pigp,
                    _("`%s' field, syntax error after reference to package `%.255s'"),
                    fip->name, dop->ed->name);
      if (fip->integer == dep_conflicts ||
          fip->integer == dep_breaks ||
          fip->integer == dep_provides ||
          fip->integer == dep_replaces)
        parse_error(ps, pigp,
                    _("alternatives (`|') not allowed in %s field"), fip->name);
      p++; while (isspace(*p)) p++;
    }
    if (!*p) break;
    p++; while (isspace(*p)) p++;
  }
}

static const char *
scan_word(const char **valp)
{
  static struct varbuf word;
  const char *p, *start, *end;

  p = *valp;
  for (;;) {
    if (!*p) {
      *valp = p;
      return NULL;
    }
    if (cisspace(*p)) {
      p++;
      continue;
    }
    start = p;
    break;
  }
  for (;;) {
    if (*p && !cisspace(*p)) {
      p++;
      continue;
    }
    end = p;
    break;
  }

  varbuf_reset(&word);
  varbuf_add_buf(&word, start, end - start);
  varbuf_end_str(&word);

  *valp = p;

  return word.buf;
}

void
f_trigpend(struct pkginfo *pend, struct pkgbin *pifp,
           struct parsedb_state *ps,
           const char *value, const struct fieldinfo *fip)
{
  const char *word, *emsg;

  if (ps->flags & pdb_rejectstatus)
    parse_error(ps, pend,
                _("value for `triggers-pending' field not allowed in "
                  "this context"));

  while ((word = scan_word(&value))) {
    emsg = trig_name_is_illegal(word);
    if (emsg)
      parse_error(ps, pend,
                  _("illegal pending trigger name `%.255s': %s"), word, emsg);

    if (!trig_note_pend_core(pend, nfstrsave(word)))
      parse_error(ps, pend,
                  _("duplicate pending trigger `%.255s'"), word);
  }
}

void
f_trigaw(struct pkginfo *aw, struct pkgbin *pifp,
         struct parsedb_state *ps,
         const char *value, const struct fieldinfo *fip)
{
  const char *word, *emsg;
  struct pkginfo *pend;
  struct pkg_spec pkgspec = PKG_SPEC_INIT(psf_def_native | psf_no_check);

  if (ps->flags & pdb_rejectstatus)
    parse_error(ps, aw,
                _("value for `triggers-awaited' field not allowed in "
                  "this context"));

  while ((word = scan_word(&value))) {
    pkg_spec_parse(&pkgspec, word);
    emsg = pkg_spec_is_illegal(&pkgspec);
    if (emsg)
      parse_error(ps, aw,
                  _("illegal package name in awaited trigger `%.255s': %s"),
                  word, emsg);
    pend = pkg_spec_find_pkg(&pkgspec, NULL);

    if (!trig_note_aw(pend, aw))
      parse_error(ps, aw,
                  _("duplicate awaited trigger package `%.255s'"), word);

    trig_enqueue_awaited_pend(pend);
  }
  pkg_spec_reset(&pkgspec);
}

void
f_forbidden(struct pkginfo *pigp, struct pkgbin *pifp,
            struct parsedb_state *ps,
            const char *value, const struct fieldinfo *fip)
{
  parse_error(ps, pigp, _("Field '%s' is not allowed in input"), fip->name);
}
