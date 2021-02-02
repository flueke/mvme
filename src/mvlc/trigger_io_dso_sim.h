#ifndef __MVME_MVLC_TRIGGER_IO_DSO_SIM_H__
#define __MVME_MVLC_TRIGGER_IO_DSO_SIM_H__

#include "mvme_context.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

// Widget for controling both the MVLC DSO and the trigger io simulation.
// Includes DSO controls, trace selection and the trace plot.
class LIBMVME_EXPORT DSOSimWidget: public QWidget
{
    Q_OBJECT
    public:
        DSOSimWidget(VMEScriptConfig *triggerIOScript, mvlc::MVLC mvlc, QWidget *parent = nullptr);
        ~DSOSimWidget() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};


} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_DSO_SIM_H__ */
