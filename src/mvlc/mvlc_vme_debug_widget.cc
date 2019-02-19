#include "mvlc/mvlc_vme_debug_widget.h"
#include "ui_vme_debug_widget.h"

namespace mesytec
{
namespace mvlc
{

VMEDebugWidget::VMEDebugWidget(MVLCObject *mvlc, QWidget *parent)
    : QWidget(parent)
    , ui(std::make_unique<Ui::VMEDebugWidget>())
    , m_mvlc(mvlc)
{
    ui->setupUi(this);
}

VMEDebugWidget::~VMEDebugWidget()
{ }

} // end namespace mvlc
} // end namespace mesytec
