#ifndef B2084B84_BA9D_4299_B403_ADFEFCF964F5
#define B2084B84_BA9D_4299_B403_ADFEFCF964F5

#include <memory>
#include <QHelpEngine>
#include <QTextBrowser>
#include <QWidget>

namespace mesytec::mvme
{
class HelpBrowser: public QTextBrowser
{
    Q_OBJECT
    public:
        HelpBrowser(QHelpEngine *helpEngine, QWidget *parent = nullptr);

    public slots:
        QVariant loadResource(int type, const QUrl &name) override;

    private:
        QHelpEngine *helpEngine_;
};

class HelpWidget: public QWidget
{
    Q_OBJECT
    public:
        HelpWidget(std::unique_ptr<QHelpEngine> &&helpEngine, QWidget *parent = nullptr);
        ~HelpWidget() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};
}

#endif /* B2084B84_BA9D_4299_B403_ADFEFCF964F5 */
