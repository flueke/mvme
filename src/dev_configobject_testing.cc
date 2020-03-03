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
