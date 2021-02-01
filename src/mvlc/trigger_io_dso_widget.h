#ifndef __MVME_MVLC_TRIGGER_IO_DSO_WIDGET_H__
#define __MVME_MVLC_TRIGGER_IO_DSO_WIDGET_H__

#include "mvme_context.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

// Widget for controling both the MVLC DSO and the trigger io simulation.
// Includes DSO controls, trace selection and the trace plot.
class LIBMVME_EXPORT DSOWidget: public QWidget
{
    Q_OBJECT
    public:
        DSOWidget(VMEScriptConfig *triggerIOScript, mvlc::MVLC mvlc, QWidget *parent = nullptr);
        ~DSOWidget() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};


} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_DSO_WIDGET_H__ */
