#include <QVariantMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <iostream>
#include "typedefs.h"

using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    QString jsonDocString;

    {
        QVariantMap theVariantMap;

        theVariantMap["s64::max"] = QString::number(std::numeric_limits<s64>::max());
        theVariantMap["s64::min"] = QString::number(std::numeric_limits<s64>::min());

        auto jsonObject = QJsonObject::fromVariantMap(theVariantMap);
        QJsonDocument jsonDoc(jsonObject);

        jsonDocString = jsonDoc.toJson();

        cout << jsonDocString.toStdString() << endl;
    }

    {
        auto jsonDoc = QJsonDocument::fromJson(jsonDocString.toLocal8Bit());
        auto jsonObject = jsonDoc.object();

        auto maxValue = jsonObject["s64::max"].toString().toLongLong();
        auto minValue = jsonObject["s64::min"].toString().toLongLong();

        cout << "parsed maxValue=" << maxValue << endl;
        cout << "parsed minValue=" << minValue << endl;
    }
}
