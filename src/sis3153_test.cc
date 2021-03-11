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
#include "sis3153.h"
#include "sis3153/sis3153eth.h"
#include <QTextStream>

static const char *DefaultSISAddress = "sis3153-0040";

int main(int argc, char *argv[])
{
    QTextStream qout(stdout);
    QString sisAddress(DefaultSISAddress);

    if (argc > 1)
    {
        sisAddress = QString(argv[1]);
    }

    qout << "Sending stop command to " << sisAddress << "\n";

    try
    {

        SIS3153 sis;
        sis.setAddress(sisAddress);

        VMEError error;

        error = sis.open();
        if (error.isError()) throw error;

        error = sis.writeRegister(SIS3153ETH_STACK_LIST_CONTROL, 1 << 16);
        if (error.isError()) throw error;

        dump_registers(&sis, [&qout](const QString &str) {
            qout << str << "\n";
        });
    }
    catch (const VMEError &e)
    {
        qout << "Error: " << e.toString() << "\n";
        return 1;
    }

    return 0;
}
