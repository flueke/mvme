#ifndef __MVME_MVLC_TRIGGER_IO_OSCI_GUI_H__
#define __MVME_MVLC_TRIGGER_IO_OSCI_GUI_H__

#include <QWidget>
#include "mvlc/mvlc_trigger_io_osci.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_osci
{

class OsciWidget: public QWidget
{
    Q_OBJECT
    public:
        OsciWidget(mvlc::MVLC &mvlc, QWidget *parent = nullptr);
        ~OsciWidget() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_OSCI_GUI_H__ */
