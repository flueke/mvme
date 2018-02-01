/* Open listfile
 *   zip
 *   flat
 * Read vmeconfig from listfile
 * build StreamInfo for iteration
 * Iterate formatting events using multievent processing if available and enabled.
 */

#include "mvme_stream_iter.h"

int main(int argc, char *argv[])
{
    using namespace mvme_stream;

    StreamInfo streamInfo;
    DataBuffer buffer(0);

    StreamIterator streamIter(streamInfo, &buffer);

    while (true)
    {
        //auto result = streamIter.next();


    }

    return 0;
}
