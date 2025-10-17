#include "test_data_filter.h"
#include "data_filter.h"
#include "analysis/a2/multiword_datafilter.h"

void TestDataFilter::test_match_mask_and_value()
{
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

void TestDataFilter::test_extract_data_()
{
    {
        auto filter = make_filter("1XDDDD11");

        QCOMPARE(get_extract_mask(filter, 'D'), 0x3cu);
        QCOMPARE(get_extract_shift(filter, 'D'), 2u);
        QCOMPARE(get_extract_bits(filter, 'D'), 4u);
        QCOMPARE(extract(filter, 0xff, 'D'), 0xfu);
        QCOMPARE(extract(filter, 0xf0, 'D'), 0xcu);
        QCOMPARE(extract(filter, 0x0f, 'D'), 0x3u);
    }

    {
        auto filter = make_filter("11XXDDDD");

        QCOMPARE(get_extract_mask(filter, 'D'), 0xfu);
        QCOMPARE(get_extract_shift(filter, 'D'), 0u);
        QCOMPARE(get_extract_bits(filter, 'D'), 4u);
        QCOMPARE(extract(filter, 0xff, 'D'), 0xfu);
        QCOMPARE(extract(filter, 0xf0, 'D'), 0x0u);
        QCOMPARE(extract(filter, 0x0f, 'D'), 0xfu);
    }

    {
        auto filter = make_filter("DDDD100110011001");

        QCOMPARE(get_extract_mask(filter, 'D'), 0xf000u);
        QCOMPARE(get_extract_shift(filter, 'D'), 12u);
        QCOMPARE(get_extract_bits(filter, 'D'), 4u);
        QCOMPARE(extract(filter, 0x7abc, 'D'), 0x7u);
        QCOMPARE(extract(filter, 0x0abc, 'D'), 0x0u);
    }

    {
        auto filter = make_filter("D0001001100110011001100110011001");

        QCOMPARE(get_extract_mask(filter, 'D'), 0x80000000u);
        QCOMPARE(get_extract_shift(filter, 'D'), 31u);
        QCOMPARE(get_extract_bits(filter, 'D'), 1u);
        QCOMPARE(extract(filter, 0x0, 'D'), 0x0u);
        QCOMPARE(extract(filter, 0xf0123456, 'D'), 0x1u);
        QCOMPARE(extract(filter, 0x70123456, 'D'), 0x0u);
    }

    {
        auto filter = make_filter("0000100110011001100110011001100D");

        QCOMPARE(get_extract_mask(filter, 'D'), 0x00000001u);
        QCOMPARE(get_extract_shift(filter, 'D'), 0u);
        QCOMPARE(get_extract_bits(filter, 'D'), 1u);
        QCOMPARE(extract(filter, 0x0, 'D'), 0x0u);
        QCOMPARE(extract(filter, 0xf012345f, 'D'), 0x1u);
    }

    // should be case insensitive
    {
        auto filter = make_filter("11XXDDDD");

        QCOMPARE(get_extract_mask(filter, 'd'), 0xfu);
        QCOMPARE(get_extract_shift(filter, 'd'), 0u);
        QCOMPARE(get_extract_bits(filter, 'd'), 4u);
        QCOMPARE(extract(filter, 0xff, 'd'), 0xfu);
        QCOMPARE(extract(filter, 0xf0, 'd'), 0x0u);
        QCOMPARE(extract(filter, 0x0f, 'd'), 0xfu);
    }

    //
    // gather tests - DataFilter
    //
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
    {
        auto filter = make_filter("11XXDDDX");
        auto cache = make_cache_entry(filter, 'd');
        QCOMPARE(cache.needGather, false);
    }
#endif

    {
        auto filter = make_filter("11XXDXDX");
        auto cache = make_cache_entry(filter, 'd');
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        QCOMPARE(cache.needGather, true);
#endif
        QCOMPARE(get_extract_bits(filter, 'd'), 2u);
        QCOMPARE(extract(filter, 0b00000000u, 'd'), 0u);
        QCOMPARE(extract(filter, 0b11001010u, 'd'), 3u);
        QCOMPARE(extract(filter, 0b11111111u, 'd'), 3u);
        QCOMPARE(extract(filter, 0b11111000u, 'd'), 2u);
    }

    {
        auto filter = make_filter("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD");
        // mask for A:                          0000 0001 0000 0011 1111 0000 0000 0000
        // sample dataWord:   0x010006fe  ->    0000 0001 0000 0000 0000 0110 1111 1110

        //QCOMPARE(filter.needGather('d'), false);
        //QCOMPARE(filter.needGather('a'), true);

        u32 dataWord = 0x010006fe;
        QCOMPARE(extract(filter, dataWord, 'a'), 1u << 6);
        QCOMPARE(extract(filter, dataWord, 'a'), 1u << 6);
    }

    //
    // gather tests - MultiWordDataFilter
    //
    {
        MultiWordDataFilter mwf({ makeFilterFromString("0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD") });

        QCOMPARE(make_cache_entry(mwf.getSubFilters()[0], 'd').needGather, false);
        QCOMPARE(make_cache_entry(mwf.getSubFilters()[0], 'a').needGather, true);

        u32 dataWord = 0x010006fe;

        mwf.handleDataWord(dataWord);

        QCOMPARE(mwf.isComplete(), true);
        QCOMPARE(mwf.extractData('A'), (u64)(1u << 6));
        QCOMPARE(mwf.getResultAddress(), (u64)(1u << 6));
    }
}

void TestDataFilter::test_data_filter_c_style_match_mask_and_value()
{
    using namespace a2::data_filter;

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

        QVERIFY(matches(filter, 0x48u));
        QVERIFY(matches(filter, 0x78u));

        QVERIFY(matches(filter, 0x4au));
        QVERIFY(matches(filter, 0x7au));

        QVERIFY(!matches(filter, 0x0fu));
        QVERIFY(!matches(filter, 0xf0u));
        QVERIFY(!matches(filter, 0xffu));
    }

    {
        auto filter = make_filter("1111XXXX0000XXXX");

        QCOMPARE(filter.matchMask, 0xf0f0u);
        QCOMPARE(filter.matchValue, 0xf000u);

        QVERIFY(matches(filter, 0xff00u));
        QVERIFY(matches(filter, 0xff0fu));
        QVERIFY(matches(filter, 0xf00fu));
    }
}

void TestDataFilter::test_data_filter_c_style_extract_data_()
{
    using namespace a2::data_filter;

    {
        auto filter = make_filter("1XDDDD11");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0x3cu);
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        QCOMPARE(cacheD.extractShift, static_cast<u8>(2u));
#endif
        QCOMPARE(cacheD.extractBits, static_cast<u8>(4u));
        QCOMPARE(extract(cacheD, 0xffu), 0xfu);
        QCOMPARE(extract(cacheD, 0xf0u), 0xcu);
        QCOMPARE(extract(cacheD, 0x0fu), 0x3u);
    }

    {
        auto filter = make_filter("11XXDDDD");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0xfu);
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        QCOMPARE(cacheD.extractShift, static_cast<u8>(0u));
#endif
        QCOMPARE(cacheD.extractBits, static_cast<u8>(4u));
        QCOMPARE(extract(cacheD, 0xffu), 0xfu);
        QCOMPARE(extract(cacheD, 0xf0u), 0x0u);
        QCOMPARE(extract(cacheD, 0x0fu), 0xfu);
    }

    {
        auto filter = make_filter("DDDD100110011001");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0xf000u);
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        QCOMPARE(cacheD.extractShift, static_cast<u8>(12u));
#endif
        QCOMPARE(cacheD.extractBits, static_cast<u8>(4u));
        QCOMPARE(extract(cacheD, 0x7abcu), 0x7u);
        QCOMPARE(extract(cacheD, 0x0abcu), 0x0u);
    }

    {
        auto filter = make_filter("D0001001100110011001100110011001");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0x80000000u);
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        QCOMPARE(cacheD.extractShift, static_cast<u8>(31u));
#endif
        QCOMPARE(cacheD.extractBits, static_cast<u8>(1u));
        QCOMPARE(extract(cacheD, 0x0u), 0x0u);
        QCOMPARE(extract(cacheD, 0xf0123456u), 0x1u);
        QCOMPARE(extract(cacheD, 0x70123456u), 0x0u);
    }

    {
        auto filter = make_filter("0000100110011001100110011001100D");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0x00000001u);
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        QCOMPARE(cacheD.extractShift, static_cast<u8>(0u));
#endif
        QCOMPARE(cacheD.extractBits, static_cast<u8>(1u));
        QCOMPARE(extract(cacheD, 0x0u), 0x0u);
        QCOMPARE(extract(cacheD, 0xf012345fu), 0x1u);
    }

    // should be case insensitive
    {
        auto filter = make_filter("11XXDDDD");
        auto cacheD = make_cache_entry(filter, 'd');

        QCOMPARE(cacheD.extractMask, 0xfu);
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        QCOMPARE(cacheD.extractShift, static_cast<u8>(0u));
#endif
        QCOMPARE(cacheD.extractBits, static_cast<u8>(4u));
        QCOMPARE(extract(cacheD, 0xffu), 0xfu);
        QCOMPARE(extract(cacheD, 0xf0u), 0x0u);
        QCOMPARE(extract(cacheD, 0x0fu), 0xfu);
    }

    //
    // gather tests - DataFilter
    //
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
    {
        auto filter = make_filter("11XXDDDX");
        auto cacheD = make_cache_entry(filter, 'd');
        QCOMPARE(cacheD.needGather, false);
    }
#endif

    {
        auto filter = make_filter("11XXDXDX");
        auto cacheD = make_cache_entry(filter, 'd');
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        QCOMPARE(cacheD.needGather, true);
#endif
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
    using namespace a2::data_filter;

    {
        MultiWordFilter mf;
        add_subfilter(&mf, make_filter("AAAADDDD"));
        add_subfilter(&mf, make_filter("DDDDAAAA"));

        QVERIFY(!process_data(&mf, 0xab));
        QVERIFY(process_data(&mf, 0xcd));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheA), (u64)(0xda));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheD), (u64)(0xcb));
        QCOMPARE(get_extract_bits(&mf, MultiWordFilter::CacheA), (u16)8);
    }

    // slow MultiWordDataFilter
    {
        MultiWordDataFilter mwf({
            makeFilterFromString("11DD DDDD DDDD DDDD DDDD DDDD DDDD DDDD"),
            makeFilterFromString("XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX"),
        });

        u32 dataWord = 0xc0001536;
        u32 wordIndex = 5;

        mwf.handleDataWord(dataWord, wordIndex);

        QVERIFY(!mwf.isComplete());

        dataWord = 0xaffeaffe;
        wordIndex = 0;

        mwf.handleDataWord(dataWord, wordIndex);

        QVERIFY(mwf.isComplete());
        QCOMPARE(mwf.getResultAddress(), (u64)(0));
        QCOMPARE(mwf.getResultValue(), (u64)(5430));
    }

    // fast MultiWordFilter
    {
        MultiWordFilter mf;
        add_subfilter(&mf, make_filter("11DD DDDD DDDD DDDD DDDD DDDD DDDD DDDD"));
        add_subfilter(&mf, make_filter("XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX"));

        u32 dataWord = 0xc0001536;
        u32 wordIndex = 5;

        QVERIFY(!process_data(&mf, dataWord, wordIndex));
        QVERIFY(!is_complete(&mf));

        dataWord = 0xaffeaffe;
        wordIndex = 0;

        QVERIFY(process_data(&mf, dataWord, wordIndex));
        QVERIFY(is_complete(&mf));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheA), (u64)(0));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheD), (u64)(5430));
    }

    // fast MultiWordFilter
    {
        MultiWordFilter mf;
        add_subfilter(&mf, make_filter("11DD DDDD DDDD DDDD DDDD DDDD DDDD DDDD"));
        add_subfilter(&mf, make_filter("XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX"));

        u32 dataWord = 0xaffeaffe;
        u32 wordIndex = 0;

        QVERIFY(!process_data(&mf, dataWord, wordIndex));
        QVERIFY(!is_complete(&mf));

        dataWord = 0xc0001536;
        wordIndex = 5;

        QVERIFY(process_data(&mf, dataWord, wordIndex));
        QVERIFY(is_complete(&mf));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheA), (u64)(0));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheD), (u64)(5430));
    }

#if 0
    {
        MultiWordFilter mf;
        add_subfilter(&mf, make_filter("11DD DDDD DDDD DDDD DDDD DDDD DDDD DDDD"));
        add_subfilter(&mf, make_filter("XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX"));

        u32 dataWord1 = 0xc0affe00;
        u32 dataWord2 = 0xbeafbeaf;

        QVERIFY(!process_data(&mf, dataWord1));
        QVERIFY(process_data(&mf, dataWord2));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheA), (u64)(0x0));
        QCOMPARE(extract(&mf, MultiWordFilter::CacheD), (u64)(dataWord1 & ~0xc0000000));
        QCOMPARE(get_extract_bits(&mf, MultiWordFilter::CacheA), (u8)0);
        QCOMPARE(get_extract_bits(&mf, MultiWordFilter::CacheD), (u8)30);
    }
#endif
}

void TestDataFilter::test_generate_pretty_filter_string()
{
    {
        QString input = "0100XXXXMMMMMMMMXXXXSSSSSSSSSSSS";
        QString pretty = "0100 XXXX MMMM MMMM XXXX SSSS SSSS SSSS";
        auto output = generate_pretty_filter_string(input);
        QCOMPARE(pretty, output);
    }

    {
        QString input = "0100 XXXX MMMM MMMM XXXX SSSS SSSS SSSS";
        auto output = generate_pretty_filter_string(input);
        QCOMPARE(input, output);
    }
}

QTEST_MAIN(TestDataFilter)
