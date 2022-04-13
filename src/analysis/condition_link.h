#ifndef __MVME_ANALYSIS_CONDITION_LINK_H__
#define __MVME_ANALYSIS_CONDITION_LINK_H__

#include "analysis/analysis_fwd.h"
#include "typedefs.h"

#include <QDebug>

#include "libmvme_export.h"

namespace analysis
{

struct LIBMVME_EXPORT ConditionLink
{
    /* The condition referenced by this link. */
    ConditionPtr condition;

    /* Subindex into the condition bits in case the condition has multiple
     * bits. */
    s32 subIndex = 0; // TODO: remove subindex as conditions now only have a single output bit

    explicit operator bool() const { return condition != nullptr; }

    bool operator==(const ConditionLink &other) const
    {
        return (condition == other.condition
                && subIndex == other.subIndex);
    }

    bool operator!=(const ConditionLink &other) const
    {
        return !(*this == other);
    }
};

#ifndef NDEBUG
inline QDebug &operator<<(QDebug& dbg, const ConditionLink &cl)
{
    QDebugStateSaver dss(dbg);

    dbg.nospace() << "ConditionLink(" << cl.condition.get() << ", " << cl.subIndex << ")";

    return dbg;
}

#endif

} // end ns analysis

#endif /* __MVME_ANALYSIS_CONDITION_LINK_H__ */
