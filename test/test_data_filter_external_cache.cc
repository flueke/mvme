#include "test_data_filter_external_cache.h"
#include "data_filter.h"

void TestDataFilterExternalCache::test_match_mask_and_value()
{
#if 0
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
#endif

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
        auto cache = filter.makeCacheEntry('D');

        QCOMPARE(cache.extractMask, 0x3cu);
        QCOMPARE(cache.extractShift, static_cast<u8>(2));
        QCOMPARE(cache.extractBits, static_cast<u8>(4u));
        QCOMPARE(filter.extractData(0xff, cache), 0xfu);
        QCOMPARE(filter.extractData(0xf0, cache), 0xcu);
        QCOMPARE(filter.extractData(0x0f, cache), 0x3u);
    }

#if 0
    {
        DataFilterExternalCache filter("11XXDDDD");

        QCOMPARE(filter.getExtractMask('D'), 0xfu);
        QCOMPARE(filter.getExtractShift('D'), 0u);
        QCOMPARE(filter.getExtractBits('D'), 4u);
        QCOMPARE(filter.extractData(0xff, 'D'), 0xfu);
        QCOMPARE(filter.extractData(0xf0, 'D'), 0x0u);
        QCOMPARE(filter.extractData(0x0f, 'D'), 0xfu);
    }

    {
        DataFilterExternalCache filter("DDDD100110011001");

        QCOMPARE(filter.getExtractMask('D'), 0xf000u);
        QCOMPARE(filter.getExtractShift('D'), 12u);
        QCOMPARE(filter.getExtractBits('D'), 4u);
        QCOMPARE(filter.extractData(0x7abc, 'D'), 0x7u);
        QCOMPARE(filter.extractData(0x0abc, 'D'), 0x0u);
    }

    {
        DataFilterExternalCache filter("D0001001100110011001100110011001");

        QCOMPARE(filter.getExtractMask('D'), 0x80000000u);
        QCOMPARE(filter.getExtractShift('D'), 31u);
        QCOMPARE(filter.getExtractBits('D'), 1u);
        QCOMPARE(filter.extractData(0x0, 'D'), 0x0u);
        QCOMPARE(filter.extractData(0xf0123456, 'D'), 0x1u);
        QCOMPARE(filter.extractData(0x70123456, 'D'), 0x0u);
    }

    {
        DataFilterExternalCache filter("0000100110011001100110011001100D");

        QCOMPARE(filter.getExtractMask('D'), 0x00000001u);
        QCOMPARE(filter.getExtractShift('D'), 0u);
        QCOMPARE(filter.getExtractBits('D'), 1u);
        QCOMPARE(filter.extractData(0x0, 'D'), 0x0u);
        QCOMPARE(filter.extractData(0xf012345f, 'D'), 0x1u);
    }

    // should be case insensitive
    {
        DataFilterExternalCache filter("11XXDDDD");

        QCOMPARE(filter.getExtractMask('d'), 0xfu);
        QCOMPARE(filter.getExtractShift('d'), 0u);
        QCOMPARE(filter.getExtractBits('d'), 4u);
        QCOMPARE(filter.extractData(0xff, 'd'), 0xfu);
        QCOMPARE(filter.extractData(0xf0, 'd'), 0x0u);
        QCOMPARE(filter.extractData(0x0f, 'd'), 0xfu);
    }

    //
    // gather tests - DataFilterExternalCache
    //
    {
        DataFilterExternalCache filter("11XXDDDX");
        QCOMPARE(filter.needGather('d'), false);
    }

    {
        DataFilterExternalCache filter("11XXDXDX");
        QCOMPARE(filter.needGather('d'), true);
        QCOMPARE(filter.getExtractBits('d'), 2u);
        QCOMPARE(filter.extractData(0b00000000u, 'd'), 0u);
        QCOMPARE(filter.extractData(0b11001010u, 'd'), 3u);
        QCOMPARE(filter.extractData(0b11111111u, 'd'), 3u);
        QCOMPARE(filter.extractData(0b11111000u, 'd'), 2u);
    }

    {
        DataFilterExternalCache filter(makeFilterFromString("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD"));
        // mask for A:                          0000 0001 0000 0011 1111 0000 0000 0000
        // sample dataWord:   0x010006fe  ->    0000 0001 0000 0000 0000 0110 1111 1110

        //QCOMPARE(filter.needGather('d'), false);
        //QCOMPARE(filter.needGather('a'), true);

        u32 dataWord = 0x010006fe;
        QCOMPARE(filter.extractData(dataWord, 'a'), 1u << 6);
        QCOMPARE(filter.extractData(dataWord, 'a'), 1u << 6);
    }

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
