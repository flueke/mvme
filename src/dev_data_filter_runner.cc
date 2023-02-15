/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
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
        err << "Empty filter given. Use --filter to specify a filter string." << '\n';
        return 1;
    }

    try
    {

        DataFilter singleFilter(makeFilterFromBytes(filterBytes));
        MultiWordDataFilter multiFilter({makeFilterFromBytes(filterBytes)});

        auto test_one_word = [&](u32 dataWord)
        {
            out << "Matching data word " << QString("0x%1").arg(dataWord, 8, 16, QLatin1Char('0')) << ":" <<  '\n';

            // single filter

            if (matches(singleFilter, dataWord))
            {
                u64 value   = extract(singleFilter, dataWord, 'D');
                u64 address = extract(singleFilter, dataWord, 'A');

                u32 extractMaskA  = get_extract_mask(singleFilter, 'A');
                u32 extractShiftA = get_extract_shift(singleFilter, 'A');
                u32 extractBitsA  = get_extract_bits(singleFilter, 'A');

                out << "  singleFilter matches: "
                    << "address=" << address
                    << ", value=" << value
                    << ", extractMask('A') = " << QString("0x%1").arg(extractMaskA, 8, 16, QLatin1Char('0'))
                    << ", extractShift('A')= " << extractShiftA
                    << ", extractBits('A') = " << extractBitsA
                    << '\n';
            }
            else
            {
                out << "  singleFilter does not match" << '\n';
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
                    << '\n';
            }
            else
            {
                out << "  multiFilter is not complete" << '\n';
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
        err << e << '\n';
        return 1;
    }

    return 0;
}
