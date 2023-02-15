/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "vme_script.h"
#include <QTextStream>
#include <QFileInfo>
#include <QDirIterator>

/* Parses input files as VMEScripts and reports any parse errors.
 *
 * Command line arguments may be single files and/or directories. Files are
 * tested directly, directories are traversed recursively and files ending in
 * ".vmescript" are checked.
 */

QTextStream out(stdout);
QTextStream err(stderr);

static void check_one_file(const QString &filename)
{
    QFile f(filename);

    if (!f.open(QIODevice::ReadOnly))
    {
        out << "EE " << filename << " -> " << f.errorString() << "\n";
        return;
    }

    try
    {
        vme_script::parse(&f);
        out << "OK " << filename << "\n";
    }
    catch (const vme_script::ParseError &e)
    {
        out << "EE " << filename << " -> " << e.toString() << "\n";
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        out << "Usage: " << argv[0] << "(file|directory)+" << "\n";
    }

    int ret = 0;

    for (s32 argi = 1; argi < argc; ++argi)
    {
        char *arg = argv[argi];
        QFileInfo fi(arg);

        if (fi.isFile())
        {
            check_one_file(fi.filePath());
        }
        else if (fi.isDir())
        {
            QDirIterator dirIter(fi.filePath(), QDirIterator::Subdirectories);

            while (dirIter.hasNext())
            {
                fi = QFileInfo(dirIter.next());

                if (fi.isFile() && fi.completeSuffix() == "vmescript")
                {
                    check_one_file(fi.filePath());
                }
            }
        }
    }

    return ret;
}
