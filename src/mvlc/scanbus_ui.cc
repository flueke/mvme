#include "mvlc/scanbus_ui.hpp"

namespace mesytec::mvme
{

struct MvlcScanbusWidget::Private
{
    explicit Private(MvlcScanbusWidget *q_)
        : q(q_)
    {
    }

    MvlcScanbusWidget *q;
};


MvlcScanbusWidget::MvlcScanbusWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>(this))
{
}

MvlcScanbusWidget::~MvlcScanbusWidget()
{
}

}
