#ifndef D3AF1733_A514_4E30_95DD_E1254899613F
#define D3AF1733_A514_4E30_95DD_E1254899613F

#include <QWidget>
#include <memory>

namespace mesytec::mvme
{

class RunNotesWidget: public QWidget
{
    Q_OBJECT
  signals:
    void textChanged(const QString &text);

  public:
    RunNotesWidget(QWidget *parent = nullptr);
    ~RunNotesWidget() override;
    QString toMarkdown() const;

  public slots:
    void setMarkdown(const QString &text);
    void setReadOnly(bool readOnly);

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mesytec::mvme

#endif /* D3AF1733_A514_4E30_95DD_E1254899613F */
