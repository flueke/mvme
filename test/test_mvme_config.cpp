#include "tests.h"

#include "mvme_config.h"

void TestMVMEConfig::test_write_json()
{
    DAQConfig daqConfig;

    {
        auto script = new VMEScriptConfig(&daqConfig);
        script->setObjectName("a script name");
        script->setScriptContents("write a32 d16 0x1234 0xBEEF");
        daqConfig.vmeScriptLists["manual"].push_back(script);
    }


    // create and print json
    QJsonObject json;
    daqConfig.write(json);
    QJsonDocument doc(json);

    auto debug(qDebug());
    debug.noquote() << "\n" << doc.toJson();
    debug.quote();
}
