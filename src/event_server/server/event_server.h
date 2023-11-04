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
#ifndef __MVME_EVENT_SERVER_H__
#define __MVME_EVENT_SERVER_H__

#include "libmvme_export.h"
#include "mvme_stream_processor.h"
#include <QHostAddress>

class LIBMVME_EXPORT EventServer: public QObject, public IStreamModuleConsumer
{
    Q_OBJECT
    signals:
        void clientConnected();
        void clientDisconnected();

    public:
        static const uint16_t Default_ListenPort = 13801;

        explicit EventServer(QObject *parent = nullptr);
        ~EventServer();

        void startup() override;
        void shutdown() override;
        void reloadConfiguration() override;

        void beginRun(const RunInfo &runInfo,
                              const VMEConfig *vmeConfig,
                              analysis::Analysis *analysis) override;

        void endRun(const DAQStats &stats, const std::exception *e = nullptr) override;

        void beginEvent(s32 eventIndex) override;
        void endEvent(s32 eventIndex) override;
        void processModuleData(s32 eventIndex, s32 moduleIndex,
                                       const u32 *data, u32 size) override;
        void processModuleData(s32 crateIndex, s32 eventIndex,
                               const ModuleData *moduleDataList, unsigned moduleCount) override;
        void processTimetick() override;
        void processSystemEvent(s32 /*crateIndex*/, const u32 */*header*/, u32 /*size*/) override {} // noop
        void setLogger(Logger logger) override;
        Logger &getLogger() override;

        // Server specific settings and info
        void setListeningInfo(const QHostAddress &address,
                              quint16 port = Default_ListenPort);

        bool isListening() const;
        size_t getNumberOfClients() const;

    public slots:
        void setEnabled(bool b);

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

#endif /* __MVME_EVENT_SERVER_H__ */
