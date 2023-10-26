/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef UUID_95ae9604_91dd_48c3_9152_7bf911c38ce7
#define UUID_95ae9604_91dd_48c3_9152_7bf911c38ce7

#include "libmvme_export.h"

extern const char LIBMVME_EXPORT GIT_SHA1[];
extern const char LIBMVME_EXPORT GIT_VERSION[];
extern const char LIBMVME_EXPORT GIT_VERSION_SHORT[];

// Digits only, e.g. "1.8.0.42"
const char * LIBMVME_EXPORT mvme_git_version();

// Includes the abbreviated git hash, e.g. "v1.8.0-1-g42663342".
const char * LIBMVME_EXPORT mvme_git_describe_version();

#endif
