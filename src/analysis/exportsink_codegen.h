#ifndef __EXPORTSINK_CODEGEN_H__
#define __EXPORTSINK_CODEGEN_H__

#include <memory>
#include <QMap>
#include <QString>

namespace analysis
{

class ExportSink;

class ExportSinkCodeGenerator
{
    public:
        ExportSinkCodeGenerator(ExportSink *sink);
        ~ExportSinkCodeGenerator();

        void generateFiles();
        QMap<QString, QString> generateMap() const;
        QStringList getOutputFilenames() const;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

}

#endif /* __EXPORTSINK_CODEGEN_H__ */
