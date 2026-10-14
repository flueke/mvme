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
        void test_data_filter_c_style_match_mask_and_value();
        void test_data_filter_c_style_extract_data_();
        void test_multiwordfilter();
        void test_generate_pretty_filter_string();
};

#endif /* __TEST_DATA_FILTER_H__ */
