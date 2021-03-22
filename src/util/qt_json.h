#ifndef __MVME_UTIL_QT_JSON_H__
#define __MVME_UTIL_QT_JSON_H__

#include <QJsonArray>
#include <QJsonValue>
#include <QVector>
#include <vector>

namespace mesytec
{
namespace mvme
{
namespace util
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

} // end namespace util
} // end namespace mvme
} // end namespace mesytec

#endif /* __MVME_UTIL_QT_JSON_H__ */
