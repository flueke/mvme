#ifndef __MVME_ANALYSIS_OBJECT_VISITOR_H__
#define __MVME_ANALYSIS_OBJECT_VISITOR_H__

#include "analysis_fwd.h"
#include "libmvme_export.h"

namespace analysis
{

class LIBMVME_EXPORT ObjectVisitor
{
    public:
        virtual void visit(SourceInterface *source) = 0;
        virtual void visit(OperatorInterface *op) = 0;
        virtual void visit(SinkInterface *sink) = 0;
        virtual void visit(Directory *dir) = 0;
};

template<typename It>
void visit_objects(It begin_, It end_, ObjectVisitor &visitor)
{
    while (begin_ != end_)
    {
        (*begin_++)->accept(visitor);
    }
}

} // end namespace analysis

#endif /* __MVME_ANALYSIS_OBJECT_VISITOR_H__ */
