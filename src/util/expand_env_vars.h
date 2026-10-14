#ifndef BAEC04D7_D422_4B19_9011_9557B59DD8BA
#define BAEC04D7_D422_4B19_9011_9557B59DD8BA

// Source: https://stackoverflow.com/a/23442780

#include <string>

namespace mesytec::mvme::util
{

// Update the input string.
void expand_env_vars( std::string & text );

// Leave input alone and return new string.
std::string expand_env_vars( const std::string & input );

}

#endif /* BAEC04D7_D422_4B19_9011_9557B59DD8BA */
