#ifndef __CPP17_ALGO_H__
#define __CPP17_ALGO_H__

#include <functional>

namespace cpp17
{
namespace std
{

// https://en.cppreference.com/w/cpp/algorithm/clamp

template<class T, class Compare>
constexpr const T& clamp( const T& v, const T& lo, const T& hi, Compare comp )
{
    return assert( !comp(hi, lo) ),
        comp(v, lo) ? lo : comp(hi, v) ? hi : v;
}

template<class T>
constexpr const T& clamp( const T& v, const T& lo, const T& hi )
{
    return clamp( v, lo, hi, ::std::less<>() );
}

} // namespace std
} // namespace cpp17

#endif /* __CPP17_ALGO_H__ */
