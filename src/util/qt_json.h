#ifndef __MVME_UTIL_QT_JSON_H__
#define __MVME_UTIL_QT_JSON_H__

#include <QJsonArray>
#include <QJsonValue>
#include <QPointF>
#include <QPolygonF>
#include <QVector>
#include <vector>

#include <qwt_interval.h>

#include "analysis/a2/a2_data_filter.h"

namespace mesytec::mvme::util
{

template<typename C>
QJsonArray container_to_json_array(const C &c)
{
    QJsonArray ret;

    for (const auto &value: c)
        ret.append(QJsonValue(value));

    return ret;
}

template<typename Converter>
auto qvector_from_json_array(const QJsonArray &a, Converter conv)
{
    using ValueType = decltype(conv(a[0]));

    QVector<ValueType> ret;

    for (auto it = std::begin(a); it != std::end(a); ++it)
        ret.push_back(conv(*it));

    return ret;
}

template<typename Converter>
auto stdvector_from_json_array(const QJsonArray &a, Converter conv)
{
    using ValueType = decltype(conv(a[0]));

    std::vector<ValueType> ret;

    for (auto it = std::begin(a); it != std::end(a); ++it)
        ret.push_back(conv(*it));

    return ret;
}

double json_value_to_double(const QJsonValue &jv)
{
    return jv.toDouble();
}

QJsonObject to_json(const QwtInterval &interval)
{
    QJsonObject result;
    result["min"] = interval.minValue();
    result["max"] = interval.maxValue();
    return result;
}

QwtInterval interval_from_json(const QJsonObject &json)
{
    QwtInterval result;
    result.setMinValue(json["min"].toDouble(make_quiet_nan()));
    result.setMaxValue(json["max"].toDouble(make_quiet_nan()));
    return result;
}

QJsonObject to_json(const QPointF &point)
{
    QJsonObject result;
    result["x"] = point.x();
    result["y"] = point.y();
    return result;
}

QPointF qpointf_from_json(const QJsonObject &json)
{
    return { json["x"].toDouble(), json["y"].toDouble() };
}

QJsonObject to_json(const QRectF &rect)
{
    QJsonObject result;

    result["topLeft"] = to_json(rect.topLeft());
    result["bottomRight"] = to_json(rect.bottomRight());

    return result;
}

QRectF qrectf_from_json(const QJsonObject &json)
{
    QRectF result(qpointf_from_json(json["topLeft"].toObject()),
                  qpointf_from_json(json["bottomRight"].toObject()));

    return result;
}

QJsonArray to_json(const QPolygonF &poly)
{
    QJsonArray points;

    for (const auto &point: poly)
    {
        points.append(to_json(point));
    }

    return points;
}

QPolygonF qpolygonf_from_json(const QJsonArray &points)
{
    QPolygonF result;
    result.reserve(points.size());

    for (auto it = points.begin(); it != points.end(); it++)
    {
        result.append(qpointf_from_json(it->toObject()));
    }

    return result;
}

} // end namespace mesytec::mvlc::util

#endif /* __MVME_UTIL_QT_JSON_H__ */
