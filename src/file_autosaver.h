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
#ifndef __MVME_FILE_AUTOSAVER_H__
#define __MVME_FILE_AUTOSAVER_H__

#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>
#include <functional>
#include "typedefs.h"

/* File auto save feature in mvme:
 *
 * Periodically save the vme and analysis configs to disk to avoid losing all
 * changes in case the program should crash.
 *
 * On opening a workspace check to see if an auto save exists and ask the user
 * if it should be loaded or deleted.
 *
 * On closing a workspace the last thing that should happen is removal of
 * existing autosaves. This has to happen after all config files have been
 * saved or the changes have been explicitly discarded by the user.
 *
 * If the user decides to load an autosave clear the filename so that it is
 * treated like a new file and a new name has to be picked
 *
 * or
 *
 * use the autosave name. !!! But this would conflict with the autosave feature
 * itself!
 *
 * Also directly opening an autosave filename could be treated like the
 * autoload (meaning clear the filename) on opening a workspace or it will
 * yield to the open autosave overwriting itself :)
 */

class FileAutoSaver: public QObject
{
    Q_OBJECT

    using Serializer = std::function<QByteArray ()>;

    signals:
        void saved(const QString &filename);
        void writeError(const QString &filename, const QString &errorMessage);

    public:
        FileAutoSaver(Serializer serializer, const QString &outputFilename, s32 interval_ms,
                      QObject *parent = nullptr);

        s32 getInterval() const { return m_timer->interval(); }
        QString getOutputFilename() const { return m_outputFilename; }
        bool isActive() { return m_timer->isActive(); }
        bool removeOutputFile();

    public slots:
        void setInterval(s32 interval_ms) { m_timer->setInterval(interval_ms); }
        void start();
        void stop();
        void saveNow();

    private:
        Serializer m_serializer;
        const QString m_outputFilename;
        QTimer *m_timer;
};

#endif /* __MVME_FILE_AUTOSAVER_H__ */
