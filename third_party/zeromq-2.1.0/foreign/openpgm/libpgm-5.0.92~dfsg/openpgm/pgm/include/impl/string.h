/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable string manipulation functions.
 *
 * Copyright (c) 2010 Miru Limited.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#pragma once
#ifndef __PGM_IMPL_STRING_H__
#define __PGM_IMPL_STRING_H__

typedef struct pgm_string_t pgm_string_t;

#include <stdarg.h>
#include <pgm/types.h>

PGM_BEGIN_DECLS

struct pgm_string_t {
	char*	str;
	size_t	len;
	size_t	allocated_len;
};

PGM_GNUC_INTERNAL char* pgm_strdup (const char*) PGM_GNUC_MALLOC;
PGM_GNUC_INTERNAL int pgm_printf_string_upper_bound (const char*, va_list) PGM_GNUC_PRINTF(1, 0);
PGM_GNUC_INTERNAL int pgm_vasprintf (char**restrict, char const*restrict, va_list args) PGM_GNUC_PRINTF(2, 0);
PGM_GNUC_INTERNAL char* pgm_strdup_vprintf (const char*, va_list) PGM_GNUC_PRINTF(1, 0) PGM_GNUC_MALLOC;
PGM_GNUC_INTERNAL char* pgm_strconcat (const char*, ...) PGM_GNUC_MALLOC PGM_GNUC_NULL_TERMINATED;
PGM_GNUC_INTERNAL char** pgm_strsplit (const char*restrict, const char*restrict, int) PGM_GNUC_MALLOC;
PGM_GNUC_INTERNAL void pgm_strfreev (char**);

PGM_GNUC_INTERNAL pgm_string_t* pgm_string_new (const char*);
PGM_GNUC_INTERNAL char* pgm_string_free (pgm_string_t*, bool);
PGM_GNUC_INTERNAL void pgm_string_printf (pgm_string_t*restrict, const char*restrict, ...) PGM_GNUC_PRINTF(2, 3);
PGM_GNUC_INTERNAL pgm_string_t* pgm_string_append (pgm_string_t*restrict, const char*restrict);
PGM_GNUC_INTERNAL pgm_string_t* pgm_string_append_c (pgm_string_t*, char);
PGM_GNUC_INTERNAL void pgm_string_append_printf (pgm_string_t*restrict, const char*restrict, ...) PGM_GNUC_PRINTF(2, 3);

PGM_END_DECLS

#endif /* __PGM_IMPL_STRING_H__ */
