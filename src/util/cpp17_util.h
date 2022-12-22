#ifndef __MVME2_SRC_UTIL_CPP17_UTIL_H_
#define __MVME2_SRC_UTIL_CPP17_UTIL_H_

// From: https://en.cppreference.com/w/cpp/utility/variant/visit

// helper type for the visitor #4
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
// explicit deduction guide (not needed as of C++20)
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

#endif // __MVME2_SRC_UTIL_CPP17_UTIL_H_