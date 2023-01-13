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

#include <getopt.h>
#include <QFile>
#include <QTextStream>

#include "databuffer.h"
#include "vme_controller.h"
#include "sis3153_util.h"

static QTextStream &print_options(QTextStream &out, struct option *opts)
{
    out << "Available command line options: ";
    bool needComma = false;

    for (; opts->name; ++opts)
    {
        if (strcmp(opts->name, "help") == 0)
            continue;

        if (needComma)
            out << ", ";

        out << opts->name;
        needComma = true;
    }
    out << "\n";

    return out;
}

static struct option long_options[] = {
    { "input-file",             required_argument,      nullptr,    0 },
    { "help",                   no_argument,            nullptr,    0 },
    { nullptr, 0, nullptr, 0 },
};

static QTextStream out(stdout);
static QTextStream err(stderr);

static qint64 checked_read(QIODevice *in, char *dest, ssize_t size)
{
    qint64 res = in->read(dest, size);

    if (res != size)
    {
        throw (QString("Error reading %1 bytes from input: %2")
               .arg(size)
               .arg(in->errorString()));
    }

    return res;
}

int main(int argc, char *argv[])
{
    QString inputFilename;

    while (true)
    {
        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c != 0)
            break;

        QString opt_name(long_options[option_index].name);

        if (opt_name == "help")
        {
            print_options(out, long_options);
            return 0;
        }
        else if (opt_name == "input-file")
        {
            inputFilename = optarg;
        }
    }

    QFile inFile;

    if (!inputFilename.isEmpty())
    {
        inFile.setFileName(inputFilename);
        if (!inFile.open(QIODevice::ReadOnly))
        {
            err << "Error opening " << inputFilename << " for reading: "
                << inFile.errorString() << "\n";
            return 1;
        }

        err << "Reading from" << inFile.fileName() << "\n";
    }
    else
    {
        if (!inFile.open(stdin, QIODevice::ReadOnly))
        {
            err << "Error reading from standard input" << "\n";
            return 1;
        }

        err << "Reading from stdin" << "\n";
    }

    int ret = 0;
    size_t nEntriesRead   = 0;
    size_t nErrorEntries  = 0;
    size_t nDataBytesRead = 0;

    try
    {
        DataBuffer readBuffer(Megabytes(1));

        while (!inFile.atEnd())
        {
            s32 errorCode   = 0;
            s32 wsaError    = 0;
            s32 bytesToRead = 0;

            checked_read(&inFile, reinterpret_cast<char *>(&errorCode), sizeof(errorCode));
            checked_read(&inFile, reinterpret_cast<char *>(&wsaError), sizeof(wsaError));
            checked_read(&inFile, reinterpret_cast<char *>(&bytesToRead), sizeof(bytesToRead));

            if (bytesToRead > 0)
            {
                readBuffer.used = 0;
                readBuffer.ensureFreeSpace(bytesToRead);
                readBuffer.used = checked_read(&inFile, reinterpret_cast<char *>(readBuffer.data), bytesToRead);
                nDataBytesRead += readBuffer.used;

                format_sis3153_buffer(&readBuffer, out, nEntriesRead);
            }
            else
            {
                ++nErrorEntries;
                VMEError error(VMEError::UnknownError, errorCode);
                out << "Entry #" << nEntriesRead << ": Error was \""
                    << error.toString() << "\" (" << error.errorCode() << ")"
                    << ", wsaError=" << wsaError
                    << "\n";
            }

            ++nEntriesRead;
        }
    }
    catch (const QString &e)
    {
        err << "!!! " << e << "\n";
        ret = 1;
    }

    out << "nEntriesRead = " << nEntriesRead
        << ", nErrorEntries = " << nErrorEntries
        << ", nDataBytesRead = " << nDataBytesRead
        << "\n";

    return ret;
}
