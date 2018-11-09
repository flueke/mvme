#ifndef __MVME_ANALYSIS_SESSION_P_H__
#define __MVME_ANALYSIS_SESSION_P_H__

class QDataStream;

namespace analysis
{

class Histo1DSink;
class Histo2DSink;
class RateMonitorSink;

namespace detail
{
    void save(QDataStream &out, const Histo1DSink *obj);
    void load(QDataStream &in, Histo1DSink *obj);

    void save(QDataStream &out, const Histo2DSink *obj);
    void load(QDataStream &in, Histo2DSink *obj);

    void save(QDataStream &out, const RateMonitorSink *obj);
    void load(QDataStream &in, RateMonitorSink *obj);

} // end namespace detail

} // end namespace analysis

#endif /* __MVME_ANALYSIS_SESSION_P_H__ */
