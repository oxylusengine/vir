#pragma once

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string_view>

#include "vir/Base.hpp"

namespace vir {
struct ThreadStack {
  struct Chunk {
    Chunk* prev = nullptr;
    usize capacity = 0;
  };

  constexpr static usize MIN_CHUNK_BYTES = 1 << 20;

  Chunk* chunk = nullptr;
  usize offset = 0;

  ThreadStack() = default;
  ThreadStack(const ThreadStack&) = delete;
  ThreadStack(ThreadStack&&) = delete;
  auto operator=(const ThreadStack&) -> ThreadStack& = delete;
  auto operator=(ThreadStack&&) -> ThreadStack& = delete;

  ~ThreadStack() {
    while (chunk) {
      auto* prev = chunk->prev;
      std::free(chunk);
      chunk = prev;
    }
  }

  static auto header_bytes() -> usize {
    return (sizeof(Chunk) + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);
  }

  auto data(this ThreadStack& self) -> u8* { return reinterpret_cast<u8*>(self.chunk) + header_bytes(); }

  auto allocate(this ThreadStack& self, usize bytes, usize alignment) -> void* {
    if (self.chunk) {
      const auto base = reinterpret_cast<uptr>(self.data());
      const auto aligned = (base + self.offset + alignment - 1) & ~(alignment - 1);
      const auto end = aligned - base + bytes;
      if (end <= self.chunk->capacity) {
        self.offset = end;
        return reinterpret_cast<void*>(aligned);
      }
    }

    auto capacity = MIN_CHUNK_BYTES;
    while (capacity < bytes + alignment) {
      capacity *= 2;
    }

    auto* memory = std::malloc(header_bytes() + capacity);
    if (!memory) {
      throw std::bad_alloc{};
    }

    auto* next = ::new (memory) Chunk{.prev = self.chunk, .capacity = capacity};
    self.chunk = next;

    const auto base = reinterpret_cast<uptr>(self.data());
    const auto aligned = (base + alignment - 1) & ~(alignment - 1);
    self.offset = aligned - base + bytes;
    return reinterpret_cast<void*>(aligned);
  }
};

inline auto get_thread_stack() -> ThreadStack& {
  thread_local ThreadStack stack;
  return stack;
}

struct ScopedStack {
  ThreadStack::Chunk* chunk = nullptr;
  usize offset = 0;

  ScopedStack() {
    auto& stack = get_thread_stack();
    if (!stack.chunk) {
      stack.allocate(0, 1);
    }

    chunk = stack.chunk;
    offset = stack.offset;
  }

  ScopedStack(const ScopedStack&) = delete;
  ScopedStack(ScopedStack&&) = delete;
  auto operator=(const ScopedStack&) -> ScopedStack& = delete;
  auto operator=(ScopedStack&&) -> ScopedStack& = delete;

  ~ScopedStack() {
    auto& stack = get_thread_stack();
    while (stack.chunk != chunk) {
      auto* prev = stack.chunk->prev;
      std::free(stack.chunk);
      stack.chunk = prev;
    }

    stack.offset = offset;
  }

  template <typename T>
  auto alloc() -> T* {
    auto* v = static_cast<T*>(get_thread_stack().allocate(sizeof(T), alignof(T)));
    std::uninitialized_default_construct_n(v, 1);
    return v;
  }

  template <typename T>
  auto alloc(usize count) -> std::span<T> {
    auto* v = static_cast<T*>(get_thread_stack().allocate(sizeof(T) * count, alignof(T)));
    std::uninitialized_default_construct_n(v, count);
    return {v, count};
  }

  auto null_terminate_cstr(std::string_view str) -> const c8* {
    auto* begin = static_cast<c8*>(get_thread_stack().allocate(str.size() + 1, alignof(c8)));
    std::memcpy(begin, str.data(), str.size());
    begin[str.size()] = '\0';
    return begin;
  }
};
} // namespace vir
