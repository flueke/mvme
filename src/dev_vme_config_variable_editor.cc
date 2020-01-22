#include <QApplication>
#include "vme_config_ui_variable_editor.h"

using vme_script::Variable;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    vme_script::SymbolTable symtab;
    symtab["foo"] = Variable("bar");
    symtab["mcst"] = Variable("bb", "", "High-byte of the multicast address in hex");
    symtab["irq"] = Variable("1", "", "IRQ value used by this event");

    SymbolTableEditorWidget editor;
    editor.resize(800, 600);
    editor.show();
    editor.setSymbolTable(symtab);

    return app.exec();
}
