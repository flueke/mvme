#ifndef BA034595_A573_46BA_8AAD_0EEDB3C4FF26
#define BA034595_A573_46BA_8AAD_0EEDB3C4FF26

#include "analysis.h"
#include "histo_ui.h"
#include "libmvme_export.h"

namespace analysis
{

class LIBMVME_EXPORT WaveformSinkWidget: public histo_ui::IPlotWidget
{
    Q_OBJECT
    public:
        WaveformSinkWidget(
            const std::shared_ptr<analysis::WaveformSink> &sink,
            AnalysisServiceProvider *asp,
            QWidget *parent = nullptr);

        ~WaveformSinkWidget() override;

        QwtPlot *getPlot() override;
        const QwtPlot *getPlot() const override;

        QToolBar *getToolBar() override;
        QStatusBar *getStatusBar() override;

    public slots:
        void replot() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif /* BA034595_A573_46BA_8AAD_0EEDB3C4FF26 */
