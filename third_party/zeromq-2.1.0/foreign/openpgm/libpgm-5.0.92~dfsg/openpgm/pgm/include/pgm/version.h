/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * OpenPGM version.
 *
 * Copyright (c) 2006-2008 Miru Limited.
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

#pragma once
#ifndef __PGM_VERSION_H__
#define __PGM_VERSION_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

extern const unsigned pgm_major_version;
extern const unsigned pgm_minor_version;
extern const unsigned pgm_micro_version;

extern const char* pgm_build_date;
extern const char* pgm_build_time;
extern const char* pgm_build_system;
extern const char* pgm_build_machine;
extern const char* pgm_build_revision;

PGM_END_DECLS

#endif /* __PGM_VERSION_H__ */
