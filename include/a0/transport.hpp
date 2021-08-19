#pragma once

#include <a0/arena.hpp>
#include <a0/c_wrap.hpp>
#include <a0/time.hpp>
#include <a0/transport.h>

#include <cstdint>
#include <functional>

namespace a0 {

using Frame = a0_transport_frame_t;

struct LockedTransport : details::CppWrap<a0_locked_transport_t> {
  bool empty() const;
  uint64_t seq_low() const;
  uint64_t seq_high() const;

  size_t used_space() const;
  void resize(size_t);

  bool ptr_valid() const;
  const Frame frame() const;
  Frame frame();

  void jump_head();
  void jump_tail();
  bool has_next() const;
  void next();
  bool has_prev() const;
  void prev();

  Frame alloc(size_t);
  bool alloc_evicts(size_t) const;

  void commit();

  void wait(std::function<bool()>);
  void wait_for(std::function<bool()>, std::chrono::nanoseconds);
  void wait_until(std::function<bool()>, TimeMono);
};

struct Transport : details::CppWrap<a0_transport_t> {
  Transport() = default;
  Transport(Arena);

  LockedTransport lock();
};

}  // namespace a0
