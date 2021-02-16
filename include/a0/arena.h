/**
 * \file arena.h
 * \rst
 *
 * Arena
 * -----
 *
 * An arena can be any contiguous memory buffer.
 *
 * For the transport, they must be zero-ed out before first initialization.
 *
 * \endrst
 */

#ifndef A0_ARENA_H
#define A0_ARENA_H

#include <a0/buf.h>
#include <a0/callback.h>
#include <a0/err.h>

#include <stdbool.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum a0_arena_mode_e {
  /// The arena may be simultaniously shared by other processes.
  /// Notification and locks are be enabled.
  A0_ARENA_MODE_SHARED,
  /// A promise that the arena will NOT be simultaniously accessed by
  /// any other processes.
  /// Notification and locks are be disabled.
  A0_ARENA_MODE_EXCLUSIVE,
  /// A promise that the arena will NOT be simultaniously written by
  /// any other processes. This process my not write to the arena.
  /// Notification and locks are be disabled.
  A0_ARENA_MODE_READONLY,
} a0_arena_mode_t;

/// An arena can be any contiguous memory buffer.
typedef struct a0_arena_s {
  /// Pointer to the contiguous memory buffer.
  a0_buf_t buf;

  /// Mode describing how the arena will be used in this process and
  /// how it is simultaniously used by other processes.
  ///
  /// Should default to A0_ARENA_MODE_SHARED. Other modes can easily
  /// corrupt the arena content if other processes write or access the
  /// arena simultaniously. Be careful.
  a0_arena_mode_t mode;
} a0_arena_t;

#ifdef __cplusplus
}
#endif

#endif  // A0_ARENA_H
