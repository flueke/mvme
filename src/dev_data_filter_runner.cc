/*
 * amplitude_all: 0000 XXXA XXXX XXAA AAAA DDDD DDDD DDDD
 * amplitude_hi : 0000 XXX1 XXXX XXAA AAAA DDDD DDDD DDDD
 * amplitude_lo : 0000 XXX0 XXXX XXAA AAAA DDDD DDDD DDDD
*/

/* data words to test:
 * 0x0003f8b0 ok
 * 0x010006fe not ok!
 */

/*
 * read a single filter string from the command line
 * create a filter using the parsed command line data
 * read input words from stdin
 *   for each word: run the filter and extract 'A' and 'D'. print the resulting values
 */

#include <getopt.h>
#include "data_filter.h"

static QTextStream in(stdin);
static QTextStream out(stdout);
static QTextStream err(stderr);

static struct option long_options[] = {
    { "filter",                 required_argument,      nullptr,    0 },
    { nullptr, 0, nullptr, 0 },
};

int main(int argc, char *argv[])
{
    QByteArray filterBytes;

    while (true)
    {
        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c != 0)
            break;

        QString opt_name(long_options[option_index].name);

        if (opt_name == "filter")
        {
            filterBytes = QByteArray(optarg);
        }
    }

    if (filterBytes.isEmpty())
    {
        err << "Empty filter given. Use --filter to specify a filter string." << endl;
        return 1;
    }

    try
    {

        DataFilter singleFilter(makeFilterFromBytes(filterBytes));
        MultiWordDataFilter multiFilter({makeFilterFromBytes(filterBytes)});

        auto test_one_word = [&](u32 dataWord)
        {
            out << "Matching data word " << QString("0x%1").arg(dataWord, 8, 16, QLatin1Char('0')) << ":" <<  endl;

            // single filter

            if (singleFilter.matches(dataWord))
            {
                u64 value   = singleFilter.extractData(dataWord, 'D');
                u64 address = singleFilter.extractData(dataWord, 'A');

                u32 extractMaskA  = singleFilter.getExtractMask('A');
                u32 extractShiftA = singleFilter.getExtractShift('A');
                u32 extractBitsA  = singleFilter.getExtractBits('A');

                out << "  singleFilter matches: "
                    << "address=" << address
                    << ", value=" << value
                    << ", extractMask('A') = " << QString("0x%1").arg(extractMaskA, 8, 16, QLatin1Char('0'))
                    << ", extractShift('A')= " << extractShiftA
                    << ", extractBits('A') = " << extractBitsA
                    << endl;
            }
            else
            {
                out << "  singleFilter does not match" << endl;
            }

            // multi filter
            multiFilter.clearCompletion();
            multiFilter.handleDataWord(dataWord);
            if (multiFilter.isComplete())
            {
                u64 value   = multiFilter.extractData('D');
                u64 address = multiFilter.extractData('A');

                out << "  multiFilter is complete: "
                    << "address=" << address
                    << ", value=" << value
                    << endl;
            }
            else
            {
                out << "  multiFilter is not complete" << endl;
            }

        };

        if (optind < argc)
        {
            while (optind < argc)
            {
                u32 dataWord = QString(argv[optind++]).toUInt(nullptr, 0);
                test_one_word(dataWord);
            }
        }
        else
        {
            while (true)
            {
                auto dataString = in.readLine();

                if (dataString.isNull())
                    break;

                u32 dataWord = dataString.toUInt(nullptr, 0);
                test_one_word(dataWord);
            }
        }
    } catch (const char *e)
    {
        err << e << endl;
        return 1;
    }

    return 0;
}
