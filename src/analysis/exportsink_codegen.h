#ifndef __EXPORTSINK_CODEGEN_H__
#define __EXPORTSINK_CODEGEN_H__

#include <memory>

struct RunInfo;

namespace analysis
{

class ExportSink;

class ExportSinkCodeGenerator
{
    public:
        ExportSinkCodeGenerator(ExportSink *sink, const RunInfo &runInfo);
        ~ExportSinkCodeGenerator();

        void generate();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

}

#endif /* __EXPORTSINK_CODEGEN_H__ */
