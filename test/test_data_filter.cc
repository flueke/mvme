#include "test_data_filter.h"
#include "data_filter.h"
#include "data_filter_c_style.h"
#include "analysis/multiword_datafilter.h"

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

    // should be case insensitive
    {
        DataFilter filter("11XXDDDD");

        QCOMPARE(filter.getExtractMask('d'), 0xfu);
        QCOMPARE(filter.getExtractShift('d'), 0u);
        QCOMPARE(filter.getExtractBits('d'), 4u);
        QCOMPARE(filter.extractData(0xff, 'd'), 0xfu);
        QCOMPARE(filter.extractData(0xf0, 'd'), 0x0u);
        QCOMPARE(filter.extractData(0x0f, 'd'), 0xfu);
    }

    //
    // gather tests - DataFilter
    //
    {
        DataFilter filter("11XXDDDX");
        QCOMPARE(filter.needGather('d'), false);
    }

    {
        DataFilter filter("11XXDXDX");
        QCOMPARE(filter.needGather('d'), true);
        QCOMPARE(filter.getExtractBits('d'), 2u);
        QCOMPARE(filter.extractData(0b00000000u, 'd'), 0u);
        QCOMPARE(filter.extractData(0b11001010u, 'd'), 3u);
        QCOMPARE(filter.extractData(0b11111111u, 'd'), 3u);
        QCOMPARE(filter.extractData(0b11111000u, 'd'), 2u);
    }

    {
        DataFilter filter(makeFilterFromString("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD"));
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
}

void TestDataFilter::test_data_filter_c_style_match_mask_and_value()
{
    using namespace data_filter;

    {
        auto filter = make_filter("1111");

        QCOMPARE(filter.matchMask, 0xfu);
        QCOMPARE(filter.matchValue, 0xfu);
    }

    {
        auto filter = make_filter("0000");

        QCOMPARE(filter.matchMask, 0xfu);
        QCOMPARE(filter.matchValue, 0x0u);
    }

    {
        auto filter = make_filter("0101");

        QCOMPARE(filter.matchMask, 0xfu);
        QCOMPARE(filter.matchValue, 0x5u);
    }

    {
        auto filter = make_filter("XXXX");

        QCOMPARE(filter.matchMask, 0x0u);
        QCOMPARE(filter.matchValue, 0x0u);
    }

    {
        auto filter = make_filter("01XX10XX");

        QCOMPARE(filter.matchMask, 0xccu);    // 1100 1100
        QCOMPARE(filter.matchValue, 0x48u);   // 0100 1000

        QVERIFY(matches(filter, 0x48));
        QVERIFY(matches(filter, 0x78));

        QVERIFY(matches(filter, 0x4a));
        QVERIFY(matches(filter, 0x7a));

        QVERIFY(!matches(filter, 0x0f));
        QVERIFY(!matches(filter, 0xf0));
        QVERIFY(!matches(filter, 0xff));
    }

    {
        auto filter = make_filter("1111XXXX0000XXXX");

        QCOMPARE(filter.matchMask, 0xf0f0u);
        QCOMPARE(filter.matchValue, 0xf000u);

        QVERIFY(matches(filter, 0xff00));
        QVERIFY(matches(filter, 0xff0f));
        QVERIFY(matches(filter, 0xf00f));
    }
}

void TestDataFilter::test_data_filter_c_style_extract_data_()
{
    using namespace data_filter;

    {
        auto filter = make_filter("1XDDDD11");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0x3cu);
        QCOMPARE(cacheD.extractShift, static_cast<u8>(2u));
        QCOMPARE(cacheD.extractBits, static_cast<u8>(4u));
        QCOMPARE(extract(cacheD, 0xff), 0xfu);
        QCOMPARE(extract(cacheD, 0xf0), 0xcu);
        QCOMPARE(extract(cacheD, 0x0f), 0x3u);
    }

    {
        auto filter = make_filter("11XXDDDD");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0xfu);
        QCOMPARE(cacheD.extractShift, static_cast<u8>(0u));
        QCOMPARE(cacheD.extractBits, static_cast<u8>(4u));
        QCOMPARE(extract(cacheD, 0xff), 0xfu);
        QCOMPARE(extract(cacheD, 0xf0), 0x0u);
        QCOMPARE(extract(cacheD, 0x0f), 0xfu);
    }

    {
        auto filter = make_filter("DDDD100110011001");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0xf000u);
        QCOMPARE(cacheD.extractShift, static_cast<u8>(12u));
        QCOMPARE(cacheD.extractBits, static_cast<u8>(4u));
        QCOMPARE(extract(cacheD, 0x7abc), 0x7u);
        QCOMPARE(extract(cacheD, 0x0abc), 0x0u);
    }

    {
        auto filter = make_filter("D0001001100110011001100110011001");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0x80000000u);
        QCOMPARE(cacheD.extractShift, static_cast<u8>(31u));
        QCOMPARE(cacheD.extractBits, static_cast<u8>(1u));
        QCOMPARE(extract(cacheD, 0x0), 0x0u);
        QCOMPARE(extract(cacheD, 0xf0123456), 0x1u);
        QCOMPARE(extract(cacheD, 0x70123456), 0x0u);
    }

    {
        auto filter = make_filter("0000100110011001100110011001100D");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0x00000001u);
        QCOMPARE(cacheD.extractShift, static_cast<u8>(0u));
        QCOMPARE(cacheD.extractBits, static_cast<u8>(1u));
        QCOMPARE(extract(cacheD, 0x0), 0x0u);
        QCOMPARE(extract(cacheD, 0xf012345f), 0x1u);
    }

    // should be case insensitive
    {
        auto filter = make_filter("11XXDDDD");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0xfu);
        QCOMPARE(cacheD.extractShift, static_cast<u8>(0u));
        QCOMPARE(cacheD.extractBits, static_cast<u8>(4u));
        QCOMPARE(extract(cacheD, 0xff), 0xfu);
        QCOMPARE(extract(cacheD, 0xf0), 0x0u);
        QCOMPARE(extract(cacheD, 0x0f), 0xfu);
    }

    //
    // gather tests - DataFilter
    //
    {
        auto filter = make_filter("11XXDDDX");
        auto cacheD = make_cache_entry(filter, 'd');
        QCOMPARE(cacheD.needGather, false);
    }

    {
        auto filter = make_filter("11XXDXDX");
        auto cacheD = make_cache_entry(filter, 'd');
        QCOMPARE(cacheD.needGather, true);
        QCOMPARE(cacheD.extractBits, static_cast<u8>(2u));
        QCOMPARE(extract(cacheD, 0b00000000u), 0u);
        QCOMPARE(extract(cacheD, 0b11001010u), 3u);
        QCOMPARE(extract(cacheD, 0b11111111u), 3u);
        QCOMPARE(extract(cacheD, 0b11111000u), 2u);
    }

    {
        auto filter = make_filter("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD");
        // mask for A:                          0000 0001 0000 0011 1111 0000 0000 0000
        // sample dataWord:   0x010006fe  ->    0000 0001 0000 0000 0000 0110 1111 1110

        auto cacheA = make_cache_entry(filter, 'a');
        u32 dataWord = 0x010006fe;
        QCOMPARE(extract(cacheA, dataWord), 1u << 6);
        QCOMPARE(extract(cacheA, dataWord), 1u << 6);
    }

    //
    // gather tests - MultiWordDataFilter
    //
    {
        MultiWordFilter mf;
        add_subfilter(&mf, make_filter("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD"));

        u32 dataWord = 0x010006fe;

        QVERIFY(process_data(&mf, dataWord));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheA), (u64)(1u << 6));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheD), (u64)(0x6fe));
    }
}

void TestDataFilter::test_multiwordfilter()
{
    using namespace data_filter;

    {
        MultiWordFilter mf;
        add_subfilter(&mf, make_filter("AAAADDDD"));
        add_subfilter(&mf, make_filter("DDDDAAAA"));

        QVERIFY(!process_data(&mf, 0xab));
        QVERIFY(process_data(&mf, 0xcd));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheA), (u64)(0xda));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheD), (u64)(0xcb));
        QCOMPARE(get_extract_bits(&mf, MultiWordFilter::CacheA), (u8)8);
    }
}

QTEST_MAIN(TestDataFilter)
