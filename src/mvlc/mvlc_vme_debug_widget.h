#ifndef __MVME_MVLC_VME_DEBUG_WIDGET_H__
#define __MVME_MVLC_VME_DEBUG_WIDGET_H__

#include "libmvme_export.h"

#include <QWidget>
#include <memory>
#include "mvlc/mvlc_qt_object.h"

namespace Ui
{
    class VMEDebugWidget;
}

namespace mesytec
{
namespace mvlc
{

class LIBMVME_EXPORT VMEDebugWidget: public QWidget
{
    Q_OBJECT
    signals:
        void sigLogMessage(const QString &msg);

    public:
        VMEDebugWidget(MVLCObject *mvlc, QWidget *parent = 0);
        virtual ~VMEDebugWidget();

    private slots:
        void slt_writeLoop_toggled(int writerIndex, bool checked);
        void slt_doWrite_clicked(int writerIndex);

        void slt_readLoop_toggled(int readerIndex, bool checked);
        void slt_doRead_clicked(int readerIndex);

        void slt_saveScript();
        void slt_loadScript();
        void slt_runScript();

    private:
        void doWrite(u32 address, u16 value);
        u16 doSingleRead(u32 address);

        struct Private;
        std::unique_ptr<Private> d;
        std::unique_ptr<Ui::VMEDebugWidget> ui;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_VME_DEBUG_WIDGET_H__ */
