/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __GUI_UTIL_H__
#define __GUI_UTIL_H__

#include <QPixmap>

class QWidget;

QWidget *make_vme_script_ref_widget();

/** Paints the pixmap created from embellishment_source onto the pixmap
 * obtained from original_source. The embellishment is painted into the
 * bottom-right corner of the source pixmap.
 * Returns the resulting pixmap.
 */
QPixmap embellish_pixmap(const QString &original_source, const QString &embellishment_source);

#endif /* __GUI_UTIL_H__ */
