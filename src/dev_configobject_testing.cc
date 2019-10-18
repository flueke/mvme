#include "vme_config.h"
#include "mvme_session.h"
#include <QDebug>
#include <QJsonDocument>

static void write(QJsonDocument &doc)
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

        auto &globalRoot = vmeConfig.getGlobalObjectRoot();
        globalRoot.addChild(container0);
    }

    {
        auto scriptConfig = new VMEScriptConfig;
        scriptConfig->setObjectName("Montags-Allergie");
        auto &globalRoot = vmeConfig.getGlobalObjectRoot();
        globalRoot.addChild(scriptConfig);
    }

    vmeConfig.write(dest);
    doc.setObject(dest);
    qDebug().noquote() << doc.toJson();
}

void read(QJsonDocument &doc)
{
    auto src = doc.object();
    VMEConfig vmeConfig;

    vmeConfig.read(src);

    qDebug() << __PRETTY_FUNCTION__ << vmeConfig.getGlobalObjectRoot().getChildren();
}

int main(int argc, char *argv[])
{
    mvme_init("dev_configobject_testing");

    QJsonDocument doc;


    qDebug() << "===== writing";
    write(doc);

    qDebug() << "===== reading";
    read(doc);

    return 0;
}
