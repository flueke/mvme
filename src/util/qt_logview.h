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
#ifndef __MVME_UTIL_QT_LOGVIEW_H__
#define __MVME_UTIL_QT_LOGVIEW_H__

#include <memory>
#include <QPlainTextEdit>
#include "libmvme_export.h"

static const size_t LogViewMaximumBlockCount = 10 * 1000u;

std::unique_ptr<QPlainTextEdit> LIBMVME_EXPORT make_logview(
    size_t maxBlockCount = LogViewMaximumBlockCount);

class MultiLogWidget: public QWidget
{
    Q_OBJECT
    public:
        MultiLogWidget(QWidget *parent = nullptr);
        ~MultiLogWidget();

    public slots:
        void appendMessage(const QString &msg, const QString &category = {});

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* __MVME_UTIL_QT_LOGVIEW_H__ */
