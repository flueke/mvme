#ifndef __MVME_MVLC_TRIGGER_IO_SCOPE_UI_H__
#define __MVME_MVLC_TRIGGER_IO_SCOPE_UI_H__

#include <QWidget>
#include "mvlc/trigger_io_scope.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_scope
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

#endif /* __MVME_MVLC_TRIGGER_IO_SCOPE_UI_H__ */
