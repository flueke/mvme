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
#include <iostream>
#include <memory>
#include <QCoreApplication>
#include "mvme_stream_iter.h"
#include "mvme_stream_util.h"
#include "vme_config.h"

using std::cout;
using std::endl;

static void dump_listfile(ListFile *listfile, VMEConfig *vmeConfig)
{
    using namespace mvme_stream;
    using Result = StreamIterator::Result;

    DataBuffer streamBuffer(Kilobytes(100));
    StreamInfo streamInfo = streaminfo_from_vmeconfig(vmeConfig, listfile->getFileVersion());
    StreamIterator streamIter(streamInfo);
    const auto &lfc = listfile_constants(streamInfo.version);
    u64 bufferNumber = 0;

    while (listfile->readSectionsIntoBuffer(&streamBuffer) > 0)
    {
        streamBuffer.id = bufferNumber++;
        streamIter.setStreamBuffer(&streamBuffer);

        qDebug() << __PRETTY_FUNCTION__ << "read buffer #" << streamBuffer.id << ", used =" << streamBuffer.used;

        while (true)
        {
             auto &result(streamIter.next());

             if (result.flags & Result::Error)
             {
                 qDebug() << __PRETTY_FUNCTION__ << "break out of read loop because of Error";
                 break;
             }

             u32 sectionHeader = *streamBuffer.indexU32(result.sectionOffset);
             u32 sectionType   = lfc.getSectionType(sectionHeader);
             u32 sectionSize   = lfc.getSectionSize(sectionHeader);

             if (result.flags & (Result::EventComplete | Result::MultiEvent))
             {
                 assert(sectionType == ListfileSections::SectionType_Event);

                 u32 crateIndex = lfc.getCrateIndex(sectionHeader);
                 u32 eventIndex = lfc.getEventIndex(sectionHeader);

                 // event section data (either from a full event or from multi event handling
                 for (size_t mi = 0; mi < result.moduleDataOffsets.size(); mi++)
                 {
                     const auto &offsets(result.moduleDataOffsets[mi]);

                     qDebug() << __PRETTY_FUNCTION__
                         << "crateIndex =" << crateIndex
                         << ", eventIndex =" << eventIndex
                         << ", moduleIndex =" << mi
                         << ", offsets.sectionHeader" << offsets.sectionHeader
                         << ", offsets.dataBegin" << offsets.dataBegin
                         << ", offsets.dataEnd" << offsets.dataEnd
                         ;

                     if (!offsets.isValid())
                         break;

                     u32 moduleSectionHeader = *streamBuffer.indexU32(offsets.sectionHeader);
                     u32 *dataBegin = streamBuffer.indexU32(offsets.dataBegin);
                     u32 *dataEnd   = streamBuffer.indexU32(offsets.dataEnd);
                     u32 dataSize   = dataEnd - dataBegin + 1;
                     u32 moduleType = lfc.getModuleType(moduleSectionHeader);

                     QString str = (QString("crateIndex=%1, eventIndex=%2, moduleIndex=%3, type=%4, sectionOffset=%5, moduleOffset=%6, dataBegin=%7, dataEnd=%8, dataSize=%9")
                                    .arg(crateIndex)
                                    .arg(eventIndex)
                                    .arg(mi)
                                    .arg(moduleType)
                                    .arg(result.sectionOffset)
                                    .arg(offsets.sectionHeader)
                                    .arg(offsets.dataBegin)
                                    .arg(offsets.dataEnd)
                                    .arg(dataSize)
                                   );

                     cout << str.toStdString() << endl;

                     qDebugOutputBuffer(reinterpret_cast<u8 *>(dataBegin), dataSize * sizeof(u32));
                 }
             }
             else if (result.flags & Result::NotSet)
             {
                 qDebug() << "result other than EventComplete";
             }

             // check if we need a new input buffer
             if (result.flags & Result::EndOfInput)
             {
                 qDebug() << __PRETTY_FUNCTION__ << "break out of read loop because of EndOfInput";
                 break;
             }
        }

        streamBuffer.used = 0;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc != 2)
    {
        cout << "Usage: " << argv[0] << " <input-listfile>" << endl;
        return 1;
    }

    const QString inFilename = argv[1];

    try
    {
        auto openResult = open_listfile(inFilename);

        if (!openResult.listfile)
            return 1;

        std::unique_ptr<VMEConfig> vmeConfig(read_config_from_listfile(openResult.listfile.get()));

        dump_listfile(openResult.listfile.get(), vmeConfig.get());
    }
    catch (const std::exception &e)
    {
        cout << e.what() << endl;
        return 1;
    }
    catch (const QString &e)
    {
        cout << e.toStdString() << endl;
        return 1;
    }

    return 0;
}
