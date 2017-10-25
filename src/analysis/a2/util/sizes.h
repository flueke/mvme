#ifndef __A2_SIZES_H__
#define __A2_SIZES_H__

inline constexpr size_t Kilobytes(size_t x) { return x * 1024; }
inline constexpr size_t Megabytes(size_t x) { return Kilobytes(x) * 1024; }
inline constexpr size_t Gigabytes(size_t x) { return Megabytes(x) * 1024; }

#endif /* __A2_SIZES_H__ */
