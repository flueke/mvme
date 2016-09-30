#ifndef __TESTS_H__
#define __TESTS_H__

#include <QtTest/QtTest>

class TestMVMEConfig: public QObject
{
    Q_OBJECT
    private slots:
        void test_write_json();
};

#endif /* __TESTS_H__ */
