/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <libsigrok/libsigrok.h>

/**
 * @file
 *
 * Version number querying functions, definitions, and macros.
 */

/**
 * @defgroup grp_versions Versions
 *
 * Version number querying functions, definitions, and macros.
 *
 * The version numbers (and/or individual components of them) can be
 * retrieved via the API calls at runtime, and/or they can be checked at
 * compile/preprocessor time using the respective macros.
 *
 * @{
 */

/**
 * Get the major libsigrok package version number.
 *
 * @return The major package version number.
 *
 * @since 0.1.0
 */
SR_API int sr_package_version_major_get(void)
{
	return SR_PACKAGE_VERSION_MAJOR;
}

/**
 * Get the minor libsigrok package version number.
 *
 * @return The minor package version number.
 *
 * @since 0.1.0
 */
SR_API int sr_package_version_minor_get(void)
{
	return SR_PACKAGE_VERSION_MINOR;
}

/**
 * Get the micro libsigrok package version number.
 *
 * @return The micro package version number.
 *
 * @since 0.1.0
 */
SR_API int sr_package_version_micro_get(void)
{
	return SR_PACKAGE_VERSION_MICRO;
}

/**
 * Get the libsigrok package version number as a string.
 *
 * @return The package version number string. The returned string is
 *         static and thus should NOT be free'd by the caller.
 *
 * @since 0.1.0
 */
SR_API const char *sr_package_version_string_get(void)
{
	return SR_PACKAGE_VERSION_STRING;
}

/** @} */
