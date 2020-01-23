#include <QApplication>
#include <QPushButton>
#include <QDebug>

#include "vme_config_ui_variable_editor.h"

using vme_script::Variable;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    vme_script::SymbolTable symtab;
    symtab["foo"] = Variable("bar");
    symtab["mesy_mcst"] = Variable("bb", "", "High-byte of the multicast address in hex");
    symtab["irq"] = Variable("1", "", "IRQ value used by this event");
    symtab["sys_irq"] = Variable("1", "", "IRQ value used by this event");
    symtab["sys_aaa"] = Variable("1", "", "IRQ value used by this event");
    symtab["system_aaa"] = Variable("1", "", "IRQ value used by this event");

    VariableEditorWidget editor;
    editor.resize(800, 600);
    editor.show();
    editor.setVariables(symtab);

    QPushButton pb_getSymbolTable("getSymbolTable");

    QObject::connect(&pb_getSymbolTable, &QPushButton::clicked,
            [&] ()
    {
        auto symtab = editor.getVariables();
        qDebug() << "variableNames =" << symtab.symbolNames();

        for (const auto &name: symtab.symbolNames())
        {
            qDebug() << "  " << name << "=" << symtab[name].value;
        }
    });

    pb_getSymbolTable.show();

    return app.exec();
}
