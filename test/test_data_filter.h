#ifndef __TEST_DATA_FILTER_H__
#define __TEST_DATA_FILTER_H__

#include <QtTest/QtTest>

class TestDataFilter: public QObject
{
    Q_OBJECT
    public:
        virtual ~TestDataFilter() {}

    private slots:
        void test_match_mask_and_value();
        void test_extract_data_();
};

#endif /* __TEST_DATA_FILTER_H__ */
