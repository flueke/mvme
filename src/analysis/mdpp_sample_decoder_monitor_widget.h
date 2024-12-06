#ifndef BD2683F6_6AE7_45D8_8DDB_2C321C828A8D
#define BD2683F6_6AE7_45D8_8DDB_2C321C828A8D

#include <memory>
#include <QMainWindow>

class AnalysisServiceProvider;

namespace analysis
{
    class DataSourceMdppSampleDecoder;
}

namespace analysis::ui
{

class MdppSampleDecoderMonitorWidget: public QMainWindow
{
    Q_OBJECT
    public:
        MdppSampleDecoderMonitorWidget(
            const std::shared_ptr<analysis::DataSourceMdppSampleDecoder> &source,
            AnalysisServiceProvider *serviceProvider,
            QWidget *parent = nullptr);
        ~MdppSampleDecoderMonitorWidget() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;

};

}

#endif /* BD2683F6_6AE7_45D8_8DDB_2C321C828A8D */
