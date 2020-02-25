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

        ExportSinkCodeGenerator(ExportSink *sink);
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
