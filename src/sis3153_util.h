#ifndef __SIS3153_UTIL_H__
#define __SIS3153_UTIL_H__

#include <QWidget>

#include "mvme_context.h"

class LIBMVME_EXPORT SIS3153DebugWidget: public QWidget
{
    Q_OBJECT
    public:
        SIS3153DebugWidget(MVMEContext *context, QWidget *parent = 0);

    private:
        void refresh();
        void resetCounters();

        MVMEContext *m_context;
        QVector<QLabel *> m_labels;
        QVector<QLabel *> m_rdoCounterLabels;
};

class LIBMVME_EXPORT SIS3153DebugToolsWidget: public QWidget
{
    Q_OBJECT
    public:
        SIS3153DebugToolsWidget(MVMEContext *context, QWidget *parent = 0);

    private:
        MVMEContext *m_context;
};

LIBMVME_EXPORT void format_sis3153_buffer(DataBuffer *buffer, QTextStream &out, u64 bufferNumber);

LIBMVME_EXPORT QString format_sis3153_stacklist_control_value(u16 value);

#endif /* __SIS3153_UTIL_H__ */
