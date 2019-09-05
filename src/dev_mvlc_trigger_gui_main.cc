#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QTableWidget>
#include <QWidget>
#include <QCheckBox>
#include <QGraphicsView>
#include <QGraphicsItem>

#include "mvlc/mvlc_trigger_io.h"

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    auto l0SetupLayout = new QHBoxLayout;

    // Timers
    {
        auto table = new QTableWidget(trigger_io::TimerCount, 3);
        table->setHorizontalHeaderLabels({"Range", "Period", "Delay"});

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(QString::number(row)));

            auto combo_range = new QComboBox;
            combo_range->addItem("ns", 0);
            combo_range->addItem("µs", 1);
            combo_range->addItem("ms", 2);
            combo_range->addItem("s",  3);

            table->setCellWidget(row, 0, combo_range);
            table->setItem(row, 1, new QTableWidgetItem(QString::number(16)));
            table->setItem(row, 2, new QTableWidgetItem(QString::number(0)));
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto gb = new QGroupBox("Timers");
        auto gb_layout = new QHBoxLayout(gb);
        gb_layout->addWidget(table);

        l0SetupLayout->addWidget(gb, 1);
    }

    // NIM IO
    {
        auto table = new QTableWidget(trigger_io::NIM_IO_Count, 6);
        table->setHorizontalHeaderLabels({"Activate", "Direction", "Delay", "Width", "Holdoff", "Invert"});

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(QString::number(row)));

            auto combo_dir = new QComboBox;
            combo_dir->addItem("IN");
            combo_dir->addItem("OUT");

            table->setCellWidget(row, 0, new QCheckBox());
            table->setCellWidget(row, 1, combo_dir);
            table->setCellWidget(row, 5, new QCheckBox());
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto gb = new QGroupBox("NIM IO");
        auto gb_layout = new QHBoxLayout(gb);
        gb_layout->addWidget(table);

        l0SetupLayout->addWidget(gb, 1);
    }

    // ECL Out
    {
        auto table = new QTableWidget(trigger_io::ECL_OUT_Count, 5);
        table->setHorizontalHeaderLabels({"Activate", "Delay", "Width", "Holdoff", "Invert"});

        for (int row = 0; row < table->rowCount(); ++row)
        {
            table->setVerticalHeaderItem(row, new QTableWidgetItem(QString::number(row)));

            table->setCellWidget(row, 0, new QCheckBox());
            table->setCellWidget(row, 4, new QCheckBox());
        }

        table->resizeColumnsToContents();
        table->resizeRowsToContents();

        auto gb = new QGroupBox("ECL OUT");
        auto gb_layout = new QHBoxLayout(gb);
        gb_layout->addWidget(table);

        l0SetupLayout->addWidget(gb, 1);
    }

    l0SetupLayout->addStretch();

    auto logicLayout = new QHBoxLayout;

    struct Level1Items
    {
        QGraphicsRectItem *parent;
        QGraphicsSimpleTextItem *label;
        std::array<QGraphicsItem *, 5> luts;
    };

    auto make_level1_items = [] () -> Level1Items
    {
        Level1Items result = {};

        QRectF lutRect(0, 0, 80, 140);

        result.parent = new QGraphicsRectItem(
            0, 0,
            2 * (lutRect.width() + 50) + 25,
            3 * (lutRect.height() + 25) + 25);
        result.parent->setPen(Qt::NoPen);
        result.parent->setBrush(QBrush("#f9f9f9"));

        for (size_t lutIdx=0; lutIdx<result.luts.size(); lutIdx++)
        {
            auto lutItem = new QGraphicsRectItem(lutRect, result.parent);
            result.luts[lutIdx] = lutItem;

            auto label = new QGraphicsSimpleTextItem(QString("LUT%1").arg(lutIdx), lutItem);
            label->moveBy((lutItem->boundingRect().width() - label->boundingRect().width()) / 2.0, 0);

            for (int input = 5; input >= 0; input--)
            {
                double r = 4;
                auto circle = new QGraphicsEllipseItem(0, 0, 2*r, 2*r, lutItem);
                circle->setPen(Qt::NoPen);
                circle->setBrush(Qt::blue);
                circle->moveBy(-circle->boundingRect().width() / 2.0,
                               -circle->boundingRect().height() / 2.0);

                const int Inputs = 6;
                double margin = 20;
                double height = lutRect.height() - 2 * margin;
                double stepHeight = height / Inputs;
                int topDownIndex = Inputs - 1 - input;

                circle->moveBy(0, margin + topDownIndex * (stepHeight + r));
            }

            for (int output = 2; output >= 0; output--)
            {
                double r = 4;
                auto circle = new QGraphicsEllipseItem(0, 0, 2*r, 2*r, lutItem);
                circle->setPen(Qt::NoPen);
                circle->setBrush(Qt::blue);
                circle->moveBy(lutRect.width() - circle->boundingRect().width() / 2.0,
                               -circle->boundingRect().height() / 2.0);

                const int Outputs = 3;
                double margin = 40;
                double height = lutRect.height() - 2 * margin;
                double stepHeight = height / Outputs;
                int topDownIndex = Outputs - 1 - output;

                circle->moveBy(0, margin + topDownIndex * (stepHeight + r));
            }
        }

        lutRect.translate(25, 25);
        result.luts[2]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[1]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[0]->setPos(lutRect.topLeft());

        lutRect.moveTo(lutRect.width() + 50, 0);
        lutRect.translate(25, 25);
        lutRect.translate(0, (lutRect.height() + 25) / 2.0);
        result.luts[4]->setPos(lutRect.topLeft());

        lutRect.translate(0, lutRect.height() + 25);
        result.luts[3]->setPos(lutRect.topLeft());

        QFont labelFont;
        labelFont.setPointSize(labelFont.pointSize() + 5);
        result.label = new QGraphicsSimpleTextItem("L1", result.parent);
        result.label->setFont(labelFont);
        result.label->moveBy(result.parent->boundingRect().width() - result.label->boundingRect().width(), 0);

        return result;
    };

    {
        auto scene = new QGraphicsScene;

        auto l1_items = make_level1_items();
        scene->addItem(l1_items.parent);

#if 0


#endif

        auto view = new QGraphicsView(scene);
        logicLayout->addWidget(view);
    }



    auto mainLayout = new QVBoxLayout;
    mainLayout->addLayout(l0SetupLayout, 30);
    mainLayout->addLayout(logicLayout,   70);

    auto mainWindow = new QWidget;
    mainWindow->setLayout(mainLayout);
    mainWindow->setAttribute(Qt::WA_DeleteOnClose);
    mainWindow->show();

    int ret =  app.exec();
    return ret;
}
