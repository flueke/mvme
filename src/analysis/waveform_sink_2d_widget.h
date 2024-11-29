#ifndef DD79B757_D240_4A88_8A34_28B40265659A
#define DD79B757_D240_4A88_8A34_28B40265659A

#include "analysis.h"
#include "histo_ui.h"
#include "libmvme_export.h"

namespace analysis
{

class LIBMVME_EXPORT WaveformSink2DWidget: public histo_ui::PlotWidget
{
    Q_OBJECT
    public:
        WaveformSink2DWidget(
            const std::shared_ptr<analysis::WaveformSink> &sink,
            AnalysisServiceProvider *asp,
            QWidget *parent = nullptr);

        ~WaveformSink2DWidget() override;

    public slots:
        void replot() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif /* DD79B757_D240_4A88_8A34_28B40265659A */
