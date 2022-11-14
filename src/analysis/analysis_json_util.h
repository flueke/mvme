#ifndef __MVME2_SRC_ANALYSIS_ANALYSIS_JSON_UTIL_H_
#define __MVME2_SRC_ANALYSIS_ANALYSIS_JSON_UTIL_H_

#include <QJsonObject>

#include "a2/a2_data_filter.h"
#include "a2/multiword_datafilter.h"

inline a2::data_filter::DataFilter a2_datafilter_from_json(const QJsonObject &json)
{
    return a2::data_filter::make_filter(
        json["filterString"].toString().toStdString(),
        json["wordIndex"].toInt());
}

inline QJsonObject to_json(const a2::data_filter::DataFilter &filter)
{
    QJsonObject result;
    result["filterString"] = QString::fromStdString(to_string(filter));
    result["wordIndex"] = filter.matchWordIndex;
    return result;
}

inline QJsonObject to_json(const a2::data_filter::MultiWordFilter &filter)
{
    QJsonObject result;

    QJsonArray subFilterArray;

    for (s32 i = 0; i < filter.filterCount; i++)
    {
        const auto &subfilter = filter.filters[i];
        auto filterJson = to_json(subfilter);
        subFilterArray.append(filterJson);
    }

    result["subFilters"] = subFilterArray;


    return result;
}

inline a2::data_filter::MultiWordFilter a2_multiwordfilter_from_json(const QJsonObject &json)
{
    a2::data_filter::MultiWordFilter result = {};

    auto subFilterArray = json["subFilters"].toArray();

    for (auto it = subFilterArray.begin();
         it != subFilterArray.end();
         it++)
    {
        add_subfilter(&result, a2_datafilter_from_json(it->toObject()));
    }

    return result;
}

inline QJsonObject to_json(const a2::data_filter::ListFilter &filter)
{
    QJsonObject result;

    result["extractionFilter"] = to_json(filter.extractionFilter);
    result["flags"] = static_cast<qint64>(filter.flags);
    result["wordCount"] = static_cast<qint64>(filter.wordCount);

    return result;
}

inline a2::data_filter::ListFilter a2_listfilter_from_json(const QJsonObject &json)
{
    using a2::data_filter::ListFilter;

    ListFilter result = {};

    result.extractionFilter = a2_multiwordfilter_from_json(json["extractionFilter"].toObject());
    result.flags = static_cast<ListFilter::Flag>(json["flags"].toInt());
    result.wordCount = static_cast<u8>(json["wordCount"].toInt());

    return result;
}


#endif // __MVME2_SRC_ANALYSIS_ANALYSIS_JSON_UTIL_H_