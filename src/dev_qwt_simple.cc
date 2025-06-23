#include <QDebug>
#include <QWidget>
#include <iostream>
#include <spdlog/spdlog.h>

#include <qwt_transform.h>
#include <qwt_plot_picker.h>

int main(int argc, char *argv[])
{
    QwtPlotPicker *picker = new QwtPlotPicker(nullptr);
    qDebug() << picker->metaObject()->className();
    spdlog::info("QwtPlotPicker class name: {}", picker->metaObject()->className());
    std::cout << "QwtPlotPicker class name: " << picker->metaObject()->className() << std::endl;

    QObject::connect(picker, &QwtPlotPicker::moved, []() { qDebug() << "Picker moved"; });

    bool b = QObject::connect(picker, qOverload<const QPoint &>(&QwtPicker::removed),
         [] {qDebug() << "Foobar";});

    //bool b = QObject::connect(picker, qOverload<const QPoint &>(&QwtPicker::removed),
    //                          [] { qDebug() << "Foobar"; });

    spdlog::info("QwtLogTransform::LogMin={}", QwtLogTransform::LogMin);
    spdlog::info("QwtLogTransform::LogMax={}", QwtLogTransform::LogMax);

    //qDebug() << get_log_min_inl() << get_log_min();
}
