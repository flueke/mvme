#include "test_data_filter_external_cache.h"
#include "data_filter.h"

void TestDataFilterExternalCache::test_match_mask_and_value()
{
    {
        DataFilterExternalCache filter("1111");

        QCOMPARE(filter.getMatchMask(), 0xfu);
        QCOMPARE(filter.getMatchValue(), 0xfu);
    }

    {
        DataFilterExternalCache filter("0000");

        QCOMPARE(filter.getMatchMask(), 0xfu);
        QCOMPARE(filter.getMatchValue(), 0x0u);
    }

    {
        DataFilterExternalCache filter("0101");

        QCOMPARE(filter.getMatchMask(), 0xfu);
        QCOMPARE(filter.getMatchValue(), 0x5u);
    }

    {
        DataFilterExternalCache filter("XXXX");

        QCOMPARE(filter.getMatchMask(), 0x0u);
        QCOMPARE(filter.getMatchValue(), 0x0u);
    }

    {
        DataFilterExternalCache filter("01XX10XX");

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
        DataFilterExternalCache filter("1111XXXX0000XXXX");

        QCOMPARE(filter.getMatchMask(), 0xf0f0u);
        QCOMPARE(filter.getMatchValue(), 0xf000u);

        QVERIFY(filter.matches(0xff00));
        QVERIFY(filter.matches(0xff0f));
        QVERIFY(filter.matches(0xf00f));
    }
}

void TestDataFilterExternalCache::test_extract_data_()
{
    {
        DataFilterExternalCache filter("1XDDDD11");
        auto fc = filter.makeCacheEntry('D');

        QCOMPARE(fc.extractMask, 0x3cu);
        QCOMPARE(fc.extractShift, static_cast<u8>(2u));
        QCOMPARE(fc.extractBits, static_cast<u8>(4u));
        QCOMPARE(filter.extractData(0xff, fc), 0xfu);
        QCOMPARE(filter.extractData(0xf0, fc), 0xcu);
        QCOMPARE(filter.extractData(0x0f, fc), 0x3u);
    }

    {
        DataFilterExternalCache filter("11XXDDDD");
        auto fc = filter.makeCacheEntry('D');

        QCOMPARE(fc.extractMask, 0xfu);
        QCOMPARE(fc.extractShift, static_cast<u8>(0u));
        QCOMPARE(fc.extractBits, static_cast<u8>(4u));
        QCOMPARE(filter.extractData(0xff, fc), 0xfu);
        QCOMPARE(filter.extractData(0xf0, fc), 0x0u);
        QCOMPARE(filter.extractData(0x0f, fc), 0xfu);
    }

    {
        DataFilterExternalCache filter("DDDD100110011001");
        auto fc = filter.makeCacheEntry('D');

        QCOMPARE(fc.extractMask, 0xf000u);
        QCOMPARE(fc.extractShift, static_cast<u8>(12u));
        QCOMPARE(fc.extractBits,  static_cast<u8>(4u));
        QCOMPARE(filter.extractData(0x7abc, fc), 0x7u);
        QCOMPARE(filter.extractData(0x0abc, fc), 0x0u);
    }

    {
        DataFilterExternalCache filter("D0001001100110011001100110011001");
        auto fc = filter.makeCacheEntry('D');

        QCOMPARE(fc.extractMask, 0x80000000u);
        QCOMPARE(fc.extractShift, static_cast<u8>(31u));
        QCOMPARE(fc.extractBits,  static_cast<u8>(1u));
        QCOMPARE(filter.extractData(0x0, fc), 0x0u);
        QCOMPARE(filter.extractData(0xf0123456, fc), 0x1u);
        QCOMPARE(filter.extractData(0x70123456, fc), 0x0u);
    }

    {
        DataFilterExternalCache filter("0000100110011001100110011001100D");
        auto fc = filter.makeCacheEntry('D');

        QCOMPARE(fc.extractMask, 0x00000001u);
        QCOMPARE(fc.extractShift, static_cast<u8>(0u));
        QCOMPARE(fc.extractBits,  static_cast<u8>(1u));
        QCOMPARE(filter.extractData(0x0, fc), 0x0u);
        QCOMPARE(filter.extractData(0xf012345f, fc), 0x1u);
    }

    // should be case insensitive
    {
        DataFilterExternalCache filter("11XXDDDD");
        auto fc = filter.makeCacheEntry('D');

        QCOMPARE(fc.extractMask, 0xfu);
        QCOMPARE(fc.extractShift, static_cast<u8>(0u));
        QCOMPARE(fc.extractBits,  static_cast<u8>(4u));
        QCOMPARE(filter.extractData(0xff, fc), 0xfu);
        QCOMPARE(filter.extractData(0xf0, fc), 0x0u);
        QCOMPARE(filter.extractData(0x0f, fc), 0xfu);
    }

    //
    // gather tests - DataFilterExternalCache
    //
    {
        DataFilterExternalCache filter("11XXDDDX");
        auto fc = filter.makeCacheEntry('D');
        QCOMPARE(fc.needGather, false);
    }

    {
        DataFilterExternalCache filter("11XXDXDX");
        auto fc = filter.makeCacheEntry('D');
        QCOMPARE(fc.needGather, true);
        QCOMPARE(fc.extractBits, static_cast<u8>(2u));
        QCOMPARE(filter.extractData(0b00000000u, fc), 0u);
        QCOMPARE(filter.extractData(0b11001010u, fc), 3u);
        QCOMPARE(filter.extractData(0b11111111u, fc), 3u);
        QCOMPARE(filter.extractData(0b11111000u, fc), 2u);
    }

    {
        DataFilterExternalCache filter("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD");
        // mask for A:                          0000 0001 0000 0011 1111 0000 0000 0000
        // sample dataWord:   0x010006fe  ->    0000 0001 0000 0000 0000 0110 1111 1110
        // masked:                                      1
        // after shift down and gather:                                        100 0000

        auto fcD = filter.makeCacheEntry('D');
        auto fcA = filter.makeCacheEntry('A');

        QCOMPARE(fcD.needGather, false);
        QCOMPARE(fcA.needGather, true);

        u32 dataWord = 0x010006fe;
        QCOMPARE(filter.extractData(dataWord, fcA), 1u << 6);
        QCOMPARE(filter.extractData(dataWord, fcA), 1u << 6);
    }

#if 0
    //
    // gather tests - MultiWordDataFilter
    //
    {
        MultiWordDataFilter mwf({ makeFilterFromString("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD") });

        QCOMPARE(mwf.getSubFilters()[0].needGather('d'), false);
        QCOMPARE(mwf.getSubFilters()[0].needGather('a'), true);

        u32 dataWord = 0x010006fe;

        mwf.handleDataWord(dataWord);

        QCOMPARE(mwf.isComplete(), true);
        QCOMPARE(mwf.extractData('A'), (u64)(1u << 6));
        QCOMPARE(mwf.getResultAddress(), (u64)(1u << 6));
    }
#endif
}

QTEST_MAIN(TestDataFilterExternalCache)
