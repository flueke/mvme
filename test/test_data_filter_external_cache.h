#ifndef __TEST_DATA_FILTER_EXTERNAL_CACHE_H__
#define __TEST_DATA_FILTER_EXTERNAL_CACHE_H__

#include <QtTest/QtTest>

class TestDataFilterExternalCache: public QObject
{
    Q_OBJECT
    public:
        virtual ~TestDataFilterExternalCache() {}

    private slots:
        void test_match_mask_and_value();
        void test_extract_data_();
};

#endif /* __TEST_DATA_FILTER_EXTERNAL_CACHE_H__ */
