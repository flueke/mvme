#include "tests.h"
#include "data_filter.h"

void TestDataFilter::test_match_mask_and_value()
{
    {
        DataFilter filter("1111");

        QCOMPARE(filter.getMatchMask(), 0xfu);
        QCOMPARE(filter.getMatchValue(), 0xfu);
    }

    {
        DataFilter filter("0000");

        QCOMPARE(filter.getMatchMask(), 0xfu);
        QCOMPARE(filter.getMatchValue(), 0x0u);
    }

    {
        DataFilter filter("0101");

        QCOMPARE(filter.getMatchMask(), 0xfu);
        QCOMPARE(filter.getMatchValue(), 0x5u);
    }

    {
        DataFilter filter("XXXX");

        QCOMPARE(filter.getMatchMask(), 0x0u);
        QCOMPARE(filter.getMatchValue(), 0x0u);
    }

    {
        DataFilter filter("01XX10XX");

        QCOMPARE(filter.getMatchMask(), 0xccu);    // 1100 1100
        QCOMPARE(filter.getMatchValue(), 0x48u);   // 0100 1000

        QVERIFY(filter.matches(0x48));
        QVERIFY(filter.matches(0x78));

        QVERIFY(filter.matches(0x4a));
        QVERIFY(filter.matches(0x7a));

        QVERIFY(!filter.matches(0x0f));
        QVERIFY(!filter.matches(0xf0));
        QVERIFY(!filter.matches(0xff));
    }

    {
        DataFilter filter("1111XXXX0000XXXX");

        QCOMPARE(filter.getMatchMask(), 0xf0f0u);
        QCOMPARE(filter.getMatchValue(), 0xf000u);

        QVERIFY(filter.matches(0xff00));
        QVERIFY(filter.matches(0xff0f));
        QVERIFY(filter.matches(0xf00f));
    }
}

void TestDataFilter::test_match_with_variables()
{
    {
        DataFilter filter("XXX1XXP0");

        QVERIFY(filter.matches(0x10)); // P=0
        QVERIFY(filter.matches(0x12)); // P=1
        QVERIFY(filter.matches(0x14)); // P=0
        QVERIFY(filter.matches(0x16)); // P=1

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));

        // set P='0'
        filter.setVariable('P', '0');

        QVERIFY(filter.matches(0x10)); // P=0
        QVERIFY(filter.matches(0x14)); // P=0

        QVERIFY(!filter.matches(0x12)); // P=1
        QVERIFY(!filter.matches(0x16)); // P=1

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));

        // set P='1'
        filter.setVariable('P', '1');

        QVERIFY(!filter.matches(0x10)); // P=0
        QVERIFY(!filter.matches(0x14)); // P=0

        QVERIFY(filter.matches(0x12)); // P=1
        QVERIFY(filter.matches(0x16)); // P=1

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));

        // set P=0
        filter.setVariable('P', 0);

        QVERIFY(filter.matches(0x10)); // P=0
        QVERIFY(filter.matches(0x14)); // P=0

        QVERIFY(!filter.matches(0x12)); // P=1
        QVERIFY(!filter.matches(0x16)); // P=1

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));

        // set P=1
        filter.setVariable('P', 1);

        QVERIFY(!filter.matches(0x10)); // P=0
        QVERIFY(!filter.matches(0x14)); // P=0

        QVERIFY(filter.matches(0x12)); // P=1
        QVERIFY(filter.matches(0x16)); // P=1

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));

        // set P='X'
        filter.setVariable('P', 'X');

        QVERIFY(filter.matches(0x10)); // P=0
        QVERIFY(filter.matches(0x12)); // P=1
        QVERIFY(filter.matches(0x14)); // P=0
        QVERIFY(filter.matches(0x16)); // P=1

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));
    }

    {
        DataFilter filter("XXP1XXP0");

        QVERIFY(filter.matches(0x10));
        QVERIFY(filter.matches(0x12));
        QVERIFY(filter.matches(0x14));
        QVERIFY(filter.matches(0x16));

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));

        // set P='0'
        filter.setVariable('P', '0');

        QVERIFY(filter.matches(0x10)); // P=0,0
        QVERIFY(filter.matches(0x14)); // P=0,0

        QVERIFY(!filter.matches(0x12)); // P=0,1
        QVERIFY(!filter.matches(0x16)); // P=0,1
        QVERIFY(!filter.matches(0x32)); // P=1,1
        QVERIFY(!filter.matches(0x30)); // P=1,0

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));

        // set P='1'
        filter.setVariable('P', '1');

        QVERIFY(!filter.matches(0x10)); // P=0,0
        QVERIFY(!filter.matches(0x14)); // P=0,0

        QVERIFY(!filter.matches(0x12)); // P=0,1
        QVERIFY(!filter.matches(0x16)); // P=0,1
        QVERIFY(!filter.matches(0x30)); // P=1,0

        QVERIFY(filter.matches(0x32)); // P=1,1
        QVERIFY(filter.matches(0x36)); // P=1,1

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));

        // set P='X'
        filter.setVariable('P', 'X');

        QVERIFY(filter.matches(0x10)); // P=0,0
        QVERIFY(filter.matches(0x12)); // P=0,1
        QVERIFY(filter.matches(0x14)); // P=0,0
        QVERIFY(filter.matches(0x16)); // P=0,1
        QVERIFY(filter.matches(0x30)); // P=1,1

        QVERIFY(!filter.matches(0x11));
        QVERIFY(!filter.matches(0x00));
    }
}

void TestDataFilter::test_extract_data_()
{
    {
        DataFilter filter("1XDDDD11");

        QCOMPARE(filter.getExtractMask('D'), 0x3cu);
        QCOMPARE(filter.getExtractShift('D'), 2u);
        QCOMPARE(filter.getExtractBits('D'), 4u);
        QCOMPARE(filter.extractData(0xff, 'D'), 0xfu);
        QCOMPARE(filter.extractData(0xf0, 'D'), 0xcu);
        QCOMPARE(filter.extractData(0x0f, 'D'), 0x3u);
    }

    {
        DataFilter filter("11XXDDDD");

        QCOMPARE(filter.getExtractMask('D'), 0xfu);
        QCOMPARE(filter.getExtractShift('D'), 0u);
        QCOMPARE(filter.getExtractBits('D'), 4u);
        QCOMPARE(filter.extractData(0xff, 'D'), 0xfu);
        QCOMPARE(filter.extractData(0xf0, 'D'), 0x0u);
        QCOMPARE(filter.extractData(0x0f, 'D'), 0xfu);
    }

    {
        DataFilter filter("DDDD100110011001");

        QCOMPARE(filter.getExtractMask('D'), 0xf000u);
        QCOMPARE(filter.getExtractShift('D'), 12u);
        QCOMPARE(filter.getExtractBits('D'), 4u);
        QCOMPARE(filter.extractData(0x7abc, 'D'), 0x7u);
        QCOMPARE(filter.extractData(0x0abc, 'D'), 0x0u);
    }

    {
        DataFilter filter("D0001001100110011001100110011001");

        QCOMPARE(filter.getExtractMask('D'), 0x80000000u);
        QCOMPARE(filter.getExtractShift('D'), 31u);
        QCOMPARE(filter.getExtractBits('D'), 1u);
        QCOMPARE(filter.extractData(0x0, 'D'), 0x0u);
        QCOMPARE(filter.extractData(0xf0123456, 'D'), 0x1u);
        QCOMPARE(filter.extractData(0x70123456, 'D'), 0x0u);
    }

    {
        DataFilter filter("0000100110011001100110011001100D");

        QCOMPARE(filter.getExtractMask('D'), 0x00000001u);
        QCOMPARE(filter.getExtractShift('D'), 0u);
        QCOMPARE(filter.getExtractBits('D'), 1u);
        QCOMPARE(filter.extractData(0x0, 'D'), 0x0u);
        QCOMPARE(filter.extractData(0xf012345f, 'D'), 0x1u);
    }
}
