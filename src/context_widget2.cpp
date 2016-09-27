#include "context_widget2.h"
#include "ui_context_widget2.h"

ContextWidget2::ContextWidget2(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ContextWidget2)
{
    ui->setupUi(this);
}

ContextWidget2::~ContextWidget2()
{
    delete ui;
}
