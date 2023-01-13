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
#ifndef __MVME_LISTFILE_UTILS_H__
#define __MVME_LISTFILE_UTILS_H__

#include <QDebug>
#include <QFile>
#include <QJsonObject>
#include <QTextStream>
#include <system_error>

#include "globals.h"
#include "data_buffer_queue.h"
#include "util.h"
#include "mvme_listfile.h"
#include "libmvme_export.h"

// Note: These structures and functions are for the older, pre-MVLC 'mvmelst'
// style listfiles. MVLC listfiles use a different code path in
// listfile_replay.cc

void dump_mvme_buffer(QTextStream &out, const DataBuffer *eventBuffer,
                      const ListfileConstants &lfc, bool dumpData=false);

class VMEConfig;
class QuaZipFile;

class LIBMVME_EXPORT ListFile
{
    public:
        // It is assumed that the input device is opened for reading when
        // passed to this constructor. The ListFile does not take ownership of
        // the input device.
        explicit ListFile(QIODevice *input = nullptr);

        bool open();
        QJsonObject getVMEConfigJSON();
        bool seekToFirstSection();
        bool readNextSection(DataBuffer *buffer);
        s32 readSectionsIntoBuffer(DataBuffer *buffer);
        const QIODevice *getInputDevice() const { return m_input; }
        qint64 size() const;
        QString getFileName() const;
        QString getFullName() const; // filename or zipname:/filename
        u32 getFileVersion() const { return m_fileVersion; }

        // Will be empty vector for version 0 or contain "MVME<version>" with
        // version being an u32.
        QVector<u8> getPreambleBuffer() const { return m_preambleBuffer; }

    private:
        bool seek(qint64 pos);

        QIODevice *m_input = nullptr;
        QJsonObject m_configJson;
        u32 m_fileVersion = 0;
        u32 m_sectionHeaderBuffer = 0;
        QVector<u8> m_preambleBuffer;
};


class LIBMVME_EXPORT ListFileWriter: public QObject
{
    Q_OBJECT
    public:
        explicit ListFileWriter(QObject *parent = 0);
        ListFileWriter(QIODevice *outputDevice, QObject *parent = 0);

        void setOutputDevice(QIODevice *device);
        QIODevice *outputDevice() const { return m_out; }
        u64 bytesWritten() const { return m_bytesWritten; }

        bool writePreamble();
        bool writeConfig(const VMEConfig *vmeConfig);
        bool writeConfig(QByteArray contents);
        bool writeBuffer(const char *buffer, size_t size);
        bool writeBuffer(const DataBuffer &buffer);
        bool writeEndSection();
        bool writeTimetickSection();
        bool writePauseSection(ListfileSections::PauseAction pauseAction);

    private:
        bool writeStringSection(ListfileSections::SectionType sectionType,
                                const QByteArray &contents);

        QIODevice *m_out = nullptr;
        u64 m_bytesWritten = 0;
};

std::pair<std::unique_ptr<VMEConfig>, std::error_code>
LIBMVME_EXPORT read_config_from_listfile(
    ListFile *listfile,
    std::function<void (const QString &msg)> logger = {});

#endif /* __MVME_LISTFILE_UTILS_H__ */
