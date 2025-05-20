#ifndef FC048370_7811_4EF9_9F39_03E96F160C7E
#define FC048370_7811_4EF9_9F39_03E96F160C7E

#include <QWidget>

namespace mesytec::mvme_mvlc
{
  class MVLC_VMEController;
}

namespace mesytec::mvme
{

class MvlcScanbusWidget: public QWidget
{
    Q_OBJECT
  public:
    MvlcScanbusWidget(QWidget *parent = nullptr);
    ~MvlcScanbusWidget() override;

  void setMvlc(mvme_mvlc::MVLC_VMEController *mvlc);

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mesytec::mvme

#endif /* FC048370_7811_4EF9_9F39_03E96F160C7E */
