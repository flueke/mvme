#include "analysis_bench.h"

#include <QCoreApplication>
#include <QSysInfo>

#include "analysis/a2_adapter.h"
#include "build_info.h"
#include "git_sha1.h"
#include "util/qt_metaobject.h"

namespace
{
QJsonArray collect_h1d_stats(const MVMEContext &mvmeContext)
{
    QJsonArray sinksArray;

    auto analysis = mvmeContext.getAnalysis();
    auto a2aState = analysis->getA2AdapterState();
    std::vector<analysis::Histo1DSink *> sinks;

    for (const auto &a1_op: a2aState->operatorMap.hash.keys())
        if (auto sink = qobject_cast<analysis::Histo1DSink *>(a1_op))
            sinks.push_back(sink);

    std::sort(std::begin(sinks), std::end(sinks),
              [] (const auto &a, const auto &b)
              {
                  return a->getId() < b->getId();
              });

    for (const auto &a1_op: sinks)
    {
        if (auto sink = qobject_cast<analysis::Histo1DSink *>(a1_op))
        {
            auto dir = analysis->getParentDirectory(sink->shared_from_this());
            auto data = analysis::get_runtime_h1dsink_data(*a2aState, sink);

            QJsonObject sinkJ;
            sinkJ["id"] = sink->getId().toString();
            sinkJ["name"] = sink->objectName();
            sinkJ["userlevel"] = sink->getUserLevel();
            sinkJ["className"] = getClassName(a1_op);

            QJsonArray histosArray;

            for (auto hi=0; hi<data->histos.size; ++hi)
            {
                const auto &a2_h1d = data->histos[hi];

                QJsonObject statsJ;
                statsJ["entryCount(a2)"] = static_cast<qint64>(a2_h1d.entryCount ? *a2_h1d.entryCount: 0);
                statsJ["underflow"] = a2_h1d.underflow ? *a2_h1d.underflow : 0.0;
                statsJ["overflow"] = a2_h1d.overflow ? *a2_h1d.overflow : 0.0;

                if (const auto a1_h1d = sink->getHisto(hi))
                {
                    auto stats = a1_h1d->calcStatistics();

                    statsJ["maxBin"] = static_cast<qint64>(stats.maxBin);
                    statsJ["maxValue"] = stats.maxValue;
                    statsJ["mean"] = stats.mean;
                    statsJ["sigma"] = stats.sigma;
                    statsJ["entryCount(a1)"] = stats.entryCount;
                    statsJ["fwhm"] = stats.fwhm;
                    statsJ["fwhmCenter"] = stats.fwhmCenter;
                }

                histosArray.append(statsJ);
            }

            sinkJ["histoStats"] = histosArray;
            sinksArray.append(sinkJ);
        }
    }

    return sinksArray;
}

QJsonArray collect_h2d_stats(const MVMEContext &mvmeContext)
{
    QJsonArray sinksArray;

    auto analysis = mvmeContext.getAnalysis();
    auto a2aState = analysis->getA2AdapterState();
    std::vector<analysis::Histo2DSink *> sinks;

    for (const auto &a1_op: a2aState->operatorMap.hash.keys())
        if (auto sink = qobject_cast<analysis::Histo2DSink *>(a1_op))
            sinks.push_back(sink);

    std::sort(std::begin(sinks), std::end(sinks),
              [] (const auto &a, const auto &b)
              {
                  return a->getId() < b->getId();
              });

    for (const auto &a1_op: sinks)
    {
        if (auto sink = qobject_cast<analysis::Histo2DSink *>(a1_op))
        {
            auto dir = analysis->getParentDirectory(sink->shared_from_this());
            auto data = analysis::get_runtime_h2dsink_data(*a2aState, sink);

            QJsonObject sinkJ;
            sinkJ["id"] = sink->getId().toString();
            sinkJ["name"] = sink->objectName();
            sinkJ["userlevel"] = sink->getUserLevel();
            sinkJ["className"] = getClassName(a1_op);

            QJsonObject histoJ;

            histoJ["entryCount(a2)"] = data->histo.entryCount;
            histoJ["nansX"] = data->histo.nans[a2::H2D::Axis::XAxis];
            histoJ["nansY"] = data->histo.nans[a2::H2D::Axis::YAxis];
            histoJ["underflowsX"] = data->histo.underflows[a2::H2D::Axis::XAxis];
            histoJ["underflowsY"] = data->histo.underflows[a2::H2D::Axis::YAxis];
            histoJ["overflowsX"] = data->histo.overflows[a2::H2D::Axis::XAxis];
            histoJ["overflowsY"] = data->histo.overflows[a2::H2D::Axis::YAxis];

            if (const auto a1_h2d = sink->getHisto())
            {
                auto stats = a1_h2d->calcGlobalStatistics();
                histoJ["maxBinX"] = static_cast<qint64>(stats.maxBinX);
                histoJ["maxBinY"] = static_cast<qint64>(stats.maxBinY);
                histoJ["maxX"] = stats.maxX;
                histoJ["maxY"] = stats.maxY;
                histoJ["maxZ"] = stats.maxZ;
                histoJ["entryCount(a1)"] = stats.entryCount;
            }

            sinkJ["histo"] = histoJ;
            sinksArray.append(sinkJ);
        }
    }

    return sinksArray;
}

QJsonObject collect_streamproc_counters(const MVMEStreamProcessorCounters &counters)
{
    QJsonObject streamProcCounters;

    auto elapsed_s = counters.startTime.msecsTo(counters.stopTime) / 1000.0;
    auto data_mb = static_cast<double>(counters.bytesProcessed) / Megabytes(1);
    auto rate_mbs = data_mb / elapsed_s;

    streamProcCounters["startTime"] = counters.startTime.toString(Qt::ISODate);
    streamProcCounters["stopTime"] = counters.stopTime.toString(Qt::ISODate);
    streamProcCounters["bytesProcessed"] = static_cast<qint64>(counters.bytesProcessed);
    streamProcCounters["buffersProcessed"] = static_cast<qint64>(counters.buffersProcessed);
    streamProcCounters["buffersWithErrors"] = static_cast<qint64>(counters.buffersWithErrors);
    streamProcCounters["totalEvents"] = static_cast<qint64>(counters.totalEvents);
    streamProcCounters["invalidEventIndices"] = static_cast<qint64>(counters.invalidEventIndices);
    streamProcCounters["elapsed_s"] = elapsed_s;
    streamProcCounters["data_mb"] = data_mb;
    streamProcCounters["rate_mbs"] = rate_mbs;

    QJsonObject eventHits;
    for (size_t ei=0; ei<counters.eventCounters.size(); ++ei)
        if (auto hits = counters.eventCounters[ei])
            eventHits[QString::number(ei)] = static_cast<qint64>(hits);

    streamProcCounters["eventHits"] = eventHits;

    QJsonObject moduleHits;
    for (size_t ei=0; ei<counters.eventCounters.size(); ++ei)
        for (size_t mi=0; mi<counters.moduleCounters[ei].size(); ++mi)
            if (auto hits = counters.moduleCounters[ei][mi])
                moduleHits[QString::number(ei) + "." + QString::number(mi)] = static_cast<qint64>(hits);

    streamProcCounters["moduleHits"] = moduleHits;

    return streamProcCounters;
}

} // end anon namespace

QJsonObject make_analysis_benchmark_info(const MVMEContext &mvmeContext)
{
    QJsonObject reportJ;
    reportJ["H1DSinks"] = collect_h1d_stats(mvmeContext);
    reportJ["H2DSinks"] = collect_h2d_stats(mvmeContext);

    if (auto streamWorker = mvmeContext.getMVMEStreamWorker())
    {
        auto countersJ = collect_streamproc_counters(streamWorker->getCounters());
        reportJ["StreamProcessorCounters"] = countersJ;
    }

    // BenchInfo
    {
        auto listfileFilename = mvmeContext.getReplayFileHandle().inputFilename;
        auto analysisFilename = mvmeContext.getAnalysisConfigFilename();

        QJsonObject infoJ;

        infoJ["build_type"] = BUILD_TYPE;
        infoJ["build_cxx_flags"] = BUILD_CXX_FLAGS;
        infoJ["git_version"] = mvme_git_version();
        infoJ["listfile"] = QFileInfo(listfileFilename).fileName();
        infoJ["program"] = QFileInfo(QCoreApplication::arguments().at(0)).fileName();
        infoJ["histoFill"] = mvmeContext.getAnalysis()->getA2AdapterState()->a2->histoFillStrategy.name();

        if (!analysisFilename.isEmpty())
            infoJ["analysis"] = QFileInfo(analysisFilename).fileName();
        else
            infoJ["analysis"] = "<from-listfile>";

        reportJ["BenchInfo"] = infoJ;
    }

    // SysInfo
    {
        QJsonObject sysInfoJ;

        sysInfoJ["buildAbi"] = QSysInfo::buildAbi();
        sysInfoJ["kernelType"] = QSysInfo::kernelType();
        sysInfoJ["kernelVersion"] = QSysInfo::kernelVersion();
        sysInfoJ["hostname"] = QSysInfo::machineHostName();
        sysInfoJ["prettyInfo"] = QSysInfo::prettyProductName();

        reportJ["SysInfo"] = sysInfoJ;
    }

    return reportJ;
}
