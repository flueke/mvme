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
#ifndef __EXPORTSINK_CODEGEN_H__
#define __EXPORTSINK_CODEGEN_H__

#include <functional>
#include <memory>
#include <QMap>
#include <QString>

namespace analysis
{

class ExportSink;

class ExportSinkCodeGenerator
{
    public:
        using Logger = std::function<void (const QString &)>;

        explicit ExportSinkCodeGenerator(ExportSink *sink);
        ~ExportSinkCodeGenerator();

        /* Instantiates code templates and writes them to the output files. */
        void generateFiles(Logger logger = Logger());

        /* Instantiates code templates and returns a mapping of
         * (outputFilename -> templateContents). */
        QMap<QString, QString> generateMap() const;

        QStringList getOutputFilenames() const;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

bool is_valid_identifier(const QString &str);

}

#endif /* __EXPORTSINK_CODEGEN_H__ */
