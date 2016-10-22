#ifndef __TESTS_H__
#define __TESTS_H__

#include <QtTest/QtTest>

class TestMVMEConfig: public QObject
{
    Q_OBJECT
    private slots:
        void test_write_json();
};

class TestDataFilter: public QObject
{
    Q_OBJECT
    private slots:
        void test_match_mask_and_value();
        void test_match_with_variables();
        void test_extract_data_();
};

#endif /* __TESTS_H__ */
