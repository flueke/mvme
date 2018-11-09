#include "analysis_session.h"

#include <QDir>
#include <QDataStream>
#include <QFile>

#include "analysis.h"

namespace
{

using namespace analysis;

QJsonObject analysis_to_json(analysis::Analysis *analysis)
{
    QJsonObject result;
    analysis->write(result);
    return result;
}

template<typename T>
QByteArray to_json(T *obj)
{
    QJsonObject json;
    obj->write(json);
    return QJsonDocument(json).toBinaryData();
}

void save(QDataStream &out, const Histo1DSink *obj)
{
}

void save(QDataStream &out, const Histo2DSink *obj)
{
}

void save(QDataStream &out, const RateMonitorSink *obj)
{
}

}

namespace analysis
{

QPair<bool, QString> save_analysis_session(
    QIODevice &outdev, analysis::Analysis *analysis)
{
    auto sinks = analysis->getSinkOperators();

    QVector<Histo1DSink *> h1dvec;
    QVector<Histo2DSink *> h2dvec;
    QVector<RateMonitorSink *> rmvec;

    for (auto sink: analysis->getSinkOperators())
    {
        if (auto obj = qobject_cast<Histo1DSink *>(sink.get()))
            h1dvec.push_back(obj);
        else if (auto obj = qobject_cast<Histo2DSink *>(sink.get()))
            h2dvec.push_back(obj);
        else if (auto obj = qobject_cast<RateMonitorSink *>(sink.get()))
            rmvec.push_back(obj);
    }

    // Format:
    //   analysis config    QByteArray
    //   runId              QString
    //   1d histograms      count prefix, then custom data * count
    //   2d histograms      as above
    //   rate monitors      as above
    QDataStream out(&outdev);
    out << to_json(analysis);
    out << analysis->getRunInfo().runId;

    out << h1dvec.size();
    for (auto obj: h1dvec)
        save(out, obj);

    out << h2dvec.size();
    for (auto obj: h2dvec)
        save(out, obj);

    out << rmvec.size();
    for (auto obj: rmvec)
        save(out, obj);

    return qMakePair(out.status() == QDataStream::Ok,
                     outdev.errorString());
}


QPair<bool, QString> load_analysis_session(
    QIODevice &in, analysis::Analysis *analysis)
{
    return qMakePair(false, QString());
}

QPair<bool, QString> save_analysis_session(
    const QString &filename, analysis::Analysis *analysis)
{
    QFile out(filename);

    if (!out.open(QIODevice::WriteOnly))
    {
        return qMakePair(false, out.errorString());
    }

    return save_analysis_session(out, analysis);
}

QPair<bool, QString> load_analysis_session(
    const QString &filename, analysis::Analysis *analysis)
{
    QFile in(filename);

    if (!in.open(QIODevice::ReadOnly))
    {
        return qMakePair(false, in.errorString());
    }

    return load_analysis_session(in, analysis);
}

QPair<QJsonDocument, QString> load_analysis_config_from_session_file(
    QIODevice &in)
{
    QPair<QJsonDocument, QString> result;

    return result;
}

QPair<QJsonDocument, QString> load_analysis_config_from_session_file(
    const QString &filename)
{
    QFile in(filename);

    if (!in.open(QIODevice::ReadOnly))
    {
        return qMakePair(QJsonDocument(), in.errorString());
    }

    return load_analysis_config_from_session_file(in);
}

} // end namespace analysis
