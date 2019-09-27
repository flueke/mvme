#include "vme_config.h"
#include <QDebug>
#include <QJsonDocument>

int main(int argc, char *argv[])
{
    QJsonObject dest;

    VMEConfig vmeConfig;

    {
        auto event0 = new EventConfig();
        event0->setObjectName("event0");

        vmeConfig.addEventConfig(event0);
    }

    {
        auto container0 = new ContainerObject;
        container0->setObjectName("cont0");
        container0->setProperty("prop cont0", "val cont0");

        auto c00 = new ContainerObject;
        c00->setObjectName("c00");
        container0->setProperty("prop c00", "val c00");
        container0->addChild(c00);

        auto scriptConfig = new VMEScriptConfig;
        scriptConfig->setObjectName("TGIF");
        scriptConfig->setScriptContents("#foobar!");

        c00->addChild(scriptConfig);

        vmeConfig.addGlobalObject(container0);
    }

    vmeConfig.write(dest);
    QJsonDocument doc(dest);

    qDebug().noquote() << doc.toJson();

    return 0;
}
