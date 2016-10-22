#include "tests.h"

#include <memory>
#include <QtTest/QtTest>

int main(int argc, char *argv[])
{
  int ret = 0;

  {
    std::list<std::shared_ptr<QObject>> tests = {
      std::make_shared<TestMVMEConfig>(),
      std::make_shared<TestDataFilter>(),
      //std::make_shared<TestFlash>(),
    };

    QCoreApplication app(argc, argv);

    for (auto obj: tests) {
      ret |= QTest::qExec(obj.get(), argc, argv);
    }
  }

  return ret;
}

