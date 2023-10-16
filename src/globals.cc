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
#include "globals.h"
#include <spdlog/spdlog.h>

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
    QString result;

    if (!(info.flags & ListFileOutputInfo::UseFormatStr))
    {
        result = info.prefix;

        if (info.flags & ListFileOutputInfo::UseRunNumber)
        {
            result += QString("run%1").arg(info.runNumber, 3, 10, QLatin1Char('0'));
        }

        if (info.flags & ListFileOutputInfo::UseTimestamp)
        {
            auto now = QDateTime::currentDateTime();
            result += QSL("_") + now.toString("yyMMdd_HHmmss");
        }

        result += info.suffix;
    }
    else
    {
        auto now = QDateTime::currentDateTime();
        auto ts = now.toString("yyMMdd_HHmmss").toStdString();
        auto s = fmt::format(info.fmtStr.toStdString(), info.runNumber, ts);
        result = QString::fromStdString(s);
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

static const int DefaultListFileCompression = 1;

void set_value(QSettings &dest, const QString &name, const QVariant &value)
{
    dest.setValue(name, value);
}

void set_value(QVariantMap &dest, const QString &name, const QVariant &value)
{
    dest[name] = value;
}

template<typename Target>
void serialize_listfile_output_info(Target &target, const ListFileOutputInfo &info)
{
    set_value(target, QSL("WriteListFile"),             info.enabled);
    set_value(target, QSL("ListFileFormat"),            toString(info.format));
    if (!info.directory.isEmpty())
        set_value(target, QSL("ListFileDirectory"),     info.directory);
    set_value(target, QSL("ListFileCompressionLevel"),  info.compressionLevel);
    set_value(target, QSL("ListFilePrefix"),            info.prefix);
    set_value(target, QSL("ListFileSuffix"),            info.suffix);
    set_value(target, QSL("ListFileFormatString"),      info.fmtStr);
    set_value(target, QSL("ListFileRunNumber"),         info.runNumber);
    set_value(target, QSL("ListFileOutputFlags"),       info.flags);
    set_value(target, QSL("ListFileSplitSize"),         static_cast<quint64>(info.splitSize));
    set_value(target, QSL("ListFileSplitTime"),         static_cast<quint64>(info.splitTime.count()));
}

template<typename Source>
void deserialize_listfile_output_info(const Source &source, ListFileOutputInfo &dest)
{
    dest.enabled          = source.value(QSL("WriteListFile"), QSL("true")).toBool();
    dest.format           = listFileFormat_fromString(source.value(QSL("ListFileFormat")).toString());
    if (dest.format == ListFileFormat::Invalid)
        dest.format = ListFileFormat::ZIP;
    dest.directory        = source.value(QSL("ListFileDirectory"), QSL("listfiles")).toString();
    dest.compressionLevel = source.value(QSL("ListFileCompressionLevel"), DefaultListFileCompression).toInt();
    dest.prefix           = source.value(QSL("ListFilePrefix"), QSL("mvmelst")).toString();
    dest.suffix           = source.value(QSL("ListFileSuffix")).toString();
    dest.fmtStr           = source.value(QSL("ListFileFormatString"), dest.fmtStr).toString();
    dest.runNumber        = source.value(QSL("ListFileRunNumber"), 1u).toUInt();
    dest.flags            = source.value(QSL("ListFileOutputFlags"), ListFileOutputInfo::UseRunNumber).toUInt();
    dest.splitSize        = source.value(QSL("ListFileSplitSize"), static_cast<quint64>(dest.splitSize)).toUInt();
    dest.splitTime        = std::chrono::seconds(
        source.value(QSL("ListFileSplitTime"), static_cast<quint64>(dest.splitTime.count())).toUInt());
}

void write_listfile_output_info_to_qsettings(const ListFileOutputInfo &info, QSettings &settings)
{
    serialize_listfile_output_info(settings, info);
}

QVariant listfile_output_info_to_variant(const ListFileOutputInfo &info)
{
    QVariantMap result;
    serialize_listfile_output_info(result, info);
    return result;
}

ListFileOutputInfo read_listfile_output_info_from_qsettings(QSettings &settings)
{
    ListFileOutputInfo result;
    deserialize_listfile_output_info(settings, result);
    return result;
}

ListFileOutputInfo listfile_output_info_from_variant(const QVariant &var)
{
    ListFileOutputInfo result;
    deserialize_listfile_output_info(var.toMap(), result);
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
