/* libguestfs
 * Copyright (C) 2018 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <config.h>

#include "guestfs.h"
#include "guestfs-internal.h"
#include "guestfs-internal-actions.h"

char *
guestfs_impl_inspect_get_osinfo (guestfs_h *g, const char *root)
{
  CLEANUP_FREE char *type = guestfs_inspect_get_type (g, root);
  CLEANUP_FREE char *distro = guestfs_inspect_get_distro (g, root);
  if (!type || !distro)
    return NULL;

  const int major = guestfs_inspect_get_major_version (g, root);
  const int minor = guestfs_inspect_get_minor_version (g, root);

  /* Linux distributions */
  if (STREQ (type, "linux")) {
    /* Table-driven mapping for most Linux distributions */
    struct {
      const char *distro;
      const char *fmt;           /* NULL = return distro name only */
      int min_major;             /* -1 = any major version */
    } const linux_map[] = {
      { "centos",    "%s%d",       8 },
      { "centos",    "%s%d.0",     7 },
      { "centos",    "%s%d.%d",    6 },
      { "circle",    "%s%d",       8 },
      { "rocky",     "%s%d",       8 },
      { "debian",    "%s%d",       4 },
      { "fedora",    "%s%d",      -1 },
      { "mageia",    "%s%d",      -1 },
      { "ubuntu",    "%s%d.%02d", -1 },
      { "archlinux", NULL,        -1 },
      { "gentoo",    NULL,        -1 },
      { "voidlinux", NULL,        -1 },
      { "altlinux",  "%s%d.%d",    8 },
      { "altlinux",  "%s%d.%d",   -1 },
      { NULL }
    };

    for (size_t i = 0; linux_map[i].distro; ++i) {
      if (STREQ (distro, linux_map[i].distro) &&
          (linux_map[i].min_major == -1 || major >= linux_map[i].min_major)) {
        if (linux_map[i].fmt)
          return safe_asprintf (g, linux_map[i].fmt, distro, major, minor);
        else
          return safe_strdup (g, distro);
      }
    }

    /* Special handling for SLES: sle15+, sles12, sles11sp3, etc. */
    if (STREQ (distro, "sles")) {
      const char *base = major >= 15 ? "sle" : "sles";
      if (minor == 0)
        return safe_asprintf (g, "%s%d", base, major);
      else
        return safe_asprintf (g, "%s%dsp%d", base, major, minor);
    }

    /* Fallback for unknown distros with version */
    if (STRNEQ (distro, "unknown") && (major > 0 || minor > 0))
      return safe_asprintf (g, "%s%d.%d", distro, major, minor);
  }

  /* BSD family */
  else if (STREQ (type, "freebsd") || STREQ (type, "netbsd") || STREQ (type, "openbsd"))
    return safe_asprintf (g, "%s%d.%d", distro, major, minor);

  /* DOS */
  else if (STREQ (type, "dos")) {
    if (STREQ (distro, "msdos"))
      return safe_strdup (g, "msdos6.22");
  }

  /* Windows */
  else if (STREQ (type, "windows")) {
    CLEANUP_FREE char *product_name = NULL;
    CLEANUP_FREE char *product_variant = NULL;
    CLEANUP_FREE char *build_id_str = NULL;
    int build_id;

    product_name = guestfs_inspect_get_product_name (g, root);
    if (!product_name)
      return NULL;
    product_variant = guestfs_inspect_get_product_variant (g, root);
    if (!product_variant)
      return NULL;

    /* Table-driven Windows version mapping */
    struct {
      int major, minor;
      const char *id;
      const char *variant_contains;  /* NULL = any */
      const char *name_contains;     /* NULL = any */
    } const win_map[] = {
      { 5, 1, "winxp",       NULL,       NULL },
      { 5, 2, "winxp",       NULL,       "XP" },
      { 5, 2, "win2k3r2",    NULL,       "R2" },
      { 5, 2, "win2k3",      NULL,       NULL },
      { 6, 0, "winvista",    NULL,       NULL },
      { 6, 0, "win2k8",      "Server",   NULL },
      { 6, 1, "win7",        NULL,       NULL },
      { 6, 1, "win2k8r2",    "Server",   NULL },
      { 6, 2, "win8",        NULL,       NULL },
      { 6, 2, "win2k12",     "Server",   NULL },
      { 6, 3, "win8.1",      NULL,       NULL },
      { 6, 3, "win2k12r2",   "Server",   NULL },
      { 10,0, "win2k25",     "Server",   "2025" },
      { 10,0, "win2k22",     "Server",   "2022" },
      { 10,0, "win2k19",     "Server",   "2019" },
      { 10,0, "win2k16",     "Server",   NULL },
      { 0 }
    };

    for (size_t i = 0; win_map[i].id; ++i) {
      if (major == win_map[i].major && minor == win_map[i].minor &&
          (!win_map[i].variant_contains ||
           strstr (product_variant, win_map[i].variant_contains)) &&
          (!win_map[i].name_contains ||
           strstr (product_name, win_map[i].name_contains))) {
        return safe_strdup (g, win_map[i].id);
      }
    }

    /* Windows >= 10 Client we can only distinguish between
     * versions by looking at the build ID. See:
     * https://learn.microsoft.com/en-us/answers/questions/586619/windows-11-build-ver-is-still-10022000194.html
     * https://github.com/cygwin/cygwin/blob/a263fe0b268580273c1adc4b1bad256147990222/winsup/cygwin/wincap.cc#L429
     */
    if (major == 10 && minor == 0 && !strstr (product_variant, "Server")) {
      build_id_str = guestfs_inspect_get_build_id (g, root);
      if (!build_id_str)
        return NULL;
      build_id = guestfs_int_parse_unsigned_int (g, build_id_str);
      if (build_id == -1)
        return NULL;
      if (build_id >= 22000)
        return safe_strdup (g, "win11");
      else
        return safe_strdup (g, "win10");
    }
  }

  /* No ID could be guessed, return "unknown". */
  return safe_strdup (g, "unknown");
}
