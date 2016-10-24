#include "hist1d.h"
#include <QLabel>

//
// Hist1D
//
Hist1D::Hist1D(u32 bits, QObject *parent)
    : QObject(parent)
    , m_bits(bits)
{
}

double Hist1D::value(u32 x) const
{
    return 0.0;
}

void Hist1D::fill(u32 x, u32 weight)
{
}

void Hist1D::inc(u32 x)
{
}

void Hist1D::clear()
{
}

//
// Hist1DWidget
//
Hist1DWidget::Hist1DWidget(Hist1D *histo, QWidget *parent)
    : MVMEWidget(parent)
    , m_histo(histo)
{
    new QLabel("Hist1D for " + m_histo->objectName(), this);
}

Hist1DWidget::~Hist1DWidget()
{
}
