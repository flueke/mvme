/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "globals.h"

QString toString(const ListFileFormat &fmt)
{
    switch (fmt)
    {
        case ListFileFormat::Invalid:
            return QSL("Invalid");
        case ListFileFormat::Plain:
            return QSL("Plain");
        case ListFileFormat::ZIP:
            return QSL("ZIP");
        case ListFileFormat::LZ4:
            return QSL("LZ4");
        case ListFileFormat::ZMQ_Ganil:
            return QSL("ZMQ_GANIL");
    }

    return QString();
}

ListFileFormat listFileFormat_fromString(const QString &str)
{
    if (str == "Plain")
        return ListFileFormat::Plain;

    if (str == "ZIP")
        return ListFileFormat::ZIP;

    if (str == "LZ4")
        return ListFileFormat::LZ4;

    if (str == "ZMQ_GANIL")
        return ListFileFormat::ZMQ_Ganil;

    return ListFileFormat::Invalid;
}

QString generate_output_basename(const ListFileOutputInfo &info)
{
    QString result(info.prefix);

    if (info.flags & ListFileOutputInfo::UseRunNumber)
    {
        result += QString("_run%1").arg(info.runNumber, 3, 10, QLatin1Char('0'));
    }

    if (info.flags & ListFileOutputInfo::UseTimestamp)
    {
        auto now = QDateTime::currentDateTime();
        result += QSL("_") + now.toString("yyMMdd_HHmmss");
    }

    return result;
}

QString generate_output_filename(const ListFileOutputInfo &info)
{
    QString result = generate_output_basename(info);

    switch (info.format)
    {
        case ListFileFormat::Plain:
            result += QSL(".mvmelst");
            break;

        case ListFileFormat::ZIP:
        case ListFileFormat::LZ4:
            result += QSL(".zip");
            break;

        default:
            break;
    }

    return result;
}

double DAQStats::getAnalysisEfficiency() const
{
    double efficiency = getAnalyzedBuffers() / static_cast<double>(totalBuffersRead);
    return efficiency;
}

const char *to_string(const ListfileBufferFormat &fmt)
{
    switch (fmt)
    {
        case ListfileBufferFormat::MVMELST:
            return "MVMELST";
        case ListfileBufferFormat::MVLC_ETH:
            return "MVLC_ETH";
        case ListfileBufferFormat::MVLC_USB:
            return "MVLC_USB";
    }

    return "unknown ListfileBufferFormat";
}

QString to_string(const AnalysisWorkerState &state)
{
    static const QMap<AnalysisWorkerState, QString> MVMEStreamWorkerState_StringTable =
    {
        { AnalysisWorkerState::Idle,              QSL("Idle") },
        { AnalysisWorkerState::Paused,            QSL("Paused") },
        { AnalysisWorkerState::Running,           QSL("Running") },
        { AnalysisWorkerState::SingleStepping,    QSL("Stepping") },
    };

    return MVMEStreamWorkerState_StringTable.value(state);
}

QString to_string(const MVMEState &state)
{
    static const QMap<MVMEState, QString> MVMEState_StringTable =
    {
        { MVMEState::Idle,                  QSL("Idle") },
        { MVMEState::Starting,              QSL("Starting") },
        { MVMEState::Running,               QSL("Running") },
        { MVMEState::Stopping,              QSL("Stopping") },
    };

    return MVMEState_StringTable.value(state);
}
