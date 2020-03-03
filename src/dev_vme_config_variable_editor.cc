/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
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
