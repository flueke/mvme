#include <memory>
#include <QApplication>
#include <QSvgGenerator>

#include "mvlc/mvlc_trigger_io_editor_p.h"
#include "mvme_session.h"
#include "qt_util.h"

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Among other things this makes the program use its own QSettings
    // file/registry key so we don't mess with other programs settings.
    mvme_init("dev_mvlc_trigger_gui_to_svg");

    trigger_io::TriggerIO ioCfg = {};
    trigger_io_config::TriggerIOGraphicsScene scene(ioCfg);

    QSvgGenerator generator;
    generator.setFileName("mvlc_trigger_io_scene.svg");
    generator.setSize(QSize(1000, 1000));
    generator.setViewBox(scene.sceneRect());

    QPainter painter;
    painter.begin(&generator);
    scene.render(&painter);
    painter.end();

    return 0;
}
