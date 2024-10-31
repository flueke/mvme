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
#ifndef __STREAM_WORKER_BASE_H__
#define __STREAM_WORKER_BASE_H__

#include <QObject>
#include <mesytec-mvlc/util/protected.h>

#include "libmvme_export.h"
#include "globals.h"
#include "stream_processor_counters.h"
#include "stream_processor_consumers.h"
#include "util/leaky_bucket.h"

class LIBMVME_EXPORT StreamWorkerBase: public QObject
{
    Q_OBJECT
    signals:
        void started();
        void stopped();
        void stateChanged(AnalysisWorkerState);
        void sigLogMessage(const QString &msg);

    public:
        enum class MessageSeverity
        {
            Info,
            Warning,
            Error
        };

        explicit StreamWorkerBase(QObject *parent = nullptr);
        virtual ~StreamWorkerBase() override;

        virtual AnalysisWorkerState getState() const = 0;

        virtual void setStartPaused(bool startPaused) = 0;
        virtual bool getStartPaused() const = 0;


        virtual MVMEStreamProcessorCounters getCounters() const = 0;

        void setAnalysis(analysis::Analysis *analysis) { ctx_.access().ref().analysis = analysis; }
        void setVMEConfig(VMEConfig *vmeConfig) { ctx_.access().ref().vmeConfig = vmeConfig; }
        void setRunInfo(const RunInfo &runInfo) { ctx_.access().ref().runInfo = runInfo; }
        void setWorkspaceDirectory(const QString &dir) { ctx_.access().ref().workspaceDir = dir; }
        void setDAQStats(const DAQStats &daqStats) { ctx_.access().ref().daqStats = daqStats; }

        // IStreamModuleConsumer
        void attachModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &consumer)
        {
            moduleConsumers_.push_back(consumer);
            consumer->setWorker(this);
        }

        void removeModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &consumer)
        {
            moduleConsumers_.removeAll(consumer);
            consumer->setWorker(nullptr);
        }

        const QVector<std::shared_ptr<IStreamModuleConsumer>> &moduleConsumers() const { return moduleConsumers_; }

        template<typename T>
        const std::shared_ptr<T> getFirstModuleConsumerOfType() const
        {
            for (const auto &c: moduleConsumers())
            {
                if (auto ret = std::dynamic_pointer_cast<T>(c))
                    return ret;
            }

            return {};
        }

        // IStreamBufferConsumer
        void attachBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &consumer)
        {
            bufferConsumers_.push_back(consumer);
            consumer->setWorker(this);
        }

        void removeBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &consumer)
        {
            bufferConsumers_.removeAll(consumer);
            consumer->setWorker(nullptr);
        }

        const QVector<std::shared_ptr<IStreamBufferConsumer>> &bufferConsumers() const { return bufferConsumers_; }

    public slots:
        // Blocking call. Returns after stop() has been invoked from the outside.
        virtual void start() = 0;

        virtual void stop(bool whenQueueEmpty = true) = 0;
        virtual void pause() = 0;
        virtual void resume() = 0;
        virtual void singleStep() = 0;

        // Used to get module and raw buffer consumers into a "running but
        // idle" state.
        virtual void startupConsumers() = 0;
        virtual void shutdownConsumers() = 0;

        // Set a delay between processing individual events in the analysis.
        // Used to slow down the whole thing to for example see waveform data
        // being accumulated.
        virtual void setArtificalDelay(const std::chrono::duration<double> &delay) { (void) delay; qDebug() << "setArtificalDelay not implemented"; }

    protected:
        // Returns true if the message was logged, false if it was suppressed due
        // to throttling.
        bool logMessage(const MessageSeverity &sev, const QString &msg, bool useThrottle = false);

        bool logInfo(const QString &msg, bool useThrottle = false)
        {
            return logMessage(MessageSeverity::Info, msg, useThrottle);
        }

        bool logWarn(const QString &msg, bool useThrottle = false)
        {
            qDebug() << __PRETTY_FUNCTION__ << msg;
            return logMessage(MessageSeverity::Warning, msg, useThrottle);
        }

        bool logError(const QString &msg, bool useThrottle = false)
        {
            return logMessage(MessageSeverity::Error, msg, useThrottle);
        }

        analysis::Analysis *getAnalysis() const { return ctx_.access()->analysis; }
        VMEConfig *getVMEConfig() const { return ctx_.access()->vmeConfig; }
        RunInfo getRunInfo() const { return ctx_.access()->runInfo; }
        QString getWorkspaceDir() const { return ctx_.access()->workspaceDir; }
        DAQStats getDAQStats() const { return ctx_.access()->daqStats; }

    private:
        static const int MaxLogMessagesPerSecond = 5;
        LeakyBucketMeter m_logThrottle;

        struct ContextHolder
        {
            analysis::Analysis *analysis = nullptr;
            VMEConfig *vmeConfig = nullptr;
            RunInfo runInfo;
            QString workspaceDir;
            DAQStats daqStats;
        };

        mutable mesytec::mvlc::Protected<ContextHolder> ctx_;
        QVector<std::shared_ptr<IStreamModuleConsumer>> moduleConsumers_;
        QVector<std::shared_ptr<IStreamBufferConsumer>> bufferConsumers_;
};

#endif /* __STREAM_WORKER_BASE_H__ */
