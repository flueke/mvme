#ifndef D1DE0040_F4F2_4734_986A_FA89AED9203D
#define D1DE0040_F4F2_4734_986A_FA89AED9203D

#include "analysis_service_provider.h"
#include <QMainWindow>

namespace analysis
{

class EventBuilderMonitorWidget: public QMainWindow
{
    Q_OBJECT

  signals:
    void aboutToClose();

  public:
    EventBuilderMonitorWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
    ~EventBuilderMonitorWidget() override;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace analysis

#endif /* D1DE0040_F4F2_4734_986A_FA89AED9203D */
