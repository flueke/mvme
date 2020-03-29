#ifndef __MESYTEC_MVLC_UTIL_STORAGE_SIZES_H__
#define __MESYTEC_MVLC_UTIL_STORAGE_SIZES_H__

#include <cstdint>

inline constexpr std::size_t Kilobytes(std::size_t x) { return x * 1024; }
inline constexpr std::size_t Megabytes(std::size_t x) { return Kilobytes(x) * 1024; }
inline constexpr std::size_t Gigabytes(std::size_t x) { return Megabytes(x) * 1024; }

#endif /* __MESYTEC_MVLC_UTIL_STORAGE_SIZES_H__ */
