#ifndef __RATE_MONITOR_BASE_H__
#define __RATE_MONITOR_BASE_H__

#include <QDebug>
#include <QRectF>
#include <QString>
#include "analysis/a2/rate_sampler.h"
#include "typedefs.h"
#include "util/counters.h"
#include "util/strings.h"
#include "util/tree.h"

using a2::RateHistoryBuffer;
using a2::RateHistoryBufferPtr;
using a2::RateSampler;

inline double get_max_value(const RateHistoryBuffer &rh, double defaultValue = 0.0)
{
    auto max_it = std::max_element(rh.begin(), rh.end());
    double max_value = (max_it == rh.end()) ? defaultValue : *max_it;
    return max_value;
}

inline QRectF get_qwt_bounding_rect(const RateHistoryBuffer &rh)
{

    double max_value = get_max_value(rh);
    auto result = QRectF(0.0, 0.0, rh.capacity(), max_value);
    return result;
}

struct RateMonitorEntry
{
    enum class Type
    {
        Group,
        SystemRate,
    };

    using Flag = u8;

    Type type = Type::Group;
    QString description;
    QString unitLabel;

    // for number formatting of rate values
    UnitScaling unitScaling = UnitScaling::Decimal;

    /* A pointer to the rate sampler for this entry. The RateSampler is owned
     * by the SamplerCollection that created it in
     * SamplerCollection::createTree(). */
    RateSampler *sampler = nullptr;

    Flag flags = 0u;

    bool operator==(const RateMonitorEntry &other) const
    {
        if (this == &other)
            return true;

        return description == other.description
            && unitLabel == other.unitLabel
            && unitScaling == other.unitScaling
            && sampler == other.sampler
            && flags == other.flags;
    }
};

using RateMonitorNode = util::tree::Node<RateMonitorEntry>;

#endif /* __RATE_MONITOR_BASE_H__ */
