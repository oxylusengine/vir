#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vir {
using namespace std::string_view_literals;

using f32 = float;
using f64 = double;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using c8 = char;
using c16 = char16_t;
using c32 = char32_t;
using b32 = u32;

using uptr = std::intptr_t;
using iptr = std::uintptr_t;
using usize = std::size_t;

constexpr auto operator""_u64(const unsigned long long n) -> u64 { return static_cast<u64>(n); }
constexpr auto operator""_i64(const unsigned long long n) -> i64 { return static_cast<i64>(n); }
constexpr auto operator""_u32(const unsigned long long n) -> u32 { return static_cast<u32>(n); }
constexpr auto operator""_i32(const unsigned long long n) -> i32 { return static_cast<i32>(n); }
constexpr auto operator""_u16(const unsigned long long n) -> u16 { return static_cast<u16>(n); }
constexpr auto operator""_i16(const unsigned long long n) -> i16 { return static_cast<i16>(n); }
constexpr auto operator""_u8(const unsigned long long n) -> u8 { return static_cast<u8>(n); }
constexpr auto operator""_i8(const unsigned long long n) -> i8 { return static_cast<i8>(n); }

constexpr auto operator""_sz(const unsigned long long n) -> usize { return static_cast<usize>(n); }
constexpr auto operator""_iptr(const unsigned long long n) -> usize { return static_cast<iptr>(n); }
constexpr auto operator""_uptr(const unsigned long long n) -> usize { return static_cast<uptr>(n); }

constexpr auto operator""_c8(const unsigned long long n) -> c8 { return static_cast<c8>(n); }
constexpr auto operator""_c16(const unsigned long long n) -> c16 { return static_cast<c16>(n); }
constexpr auto operator""_c32(const unsigned long long n) -> c32 { return static_cast<c32>(n); }

template <class... T>
struct match : T... {
  using T::operator()...;
};

template <typename T>
concept Container = requires(T& t) {
  { t.begin() };
  { t.end() };
};

template <Container T>
inline auto size_bytes(const T& v) -> usize {
  return v.size() * sizeof(typename T::value_type);
}

template <typename T, usize N>
consteval auto size_bytes(T (&)[N]) -> usize {
  return N * sizeof(T);
}
} // namespace vir
