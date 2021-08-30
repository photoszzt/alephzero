#include <a0/err.h>
#include <a0/inline.h>
#include <a0/mtx.h>
#include <a0/tid.h>
#include <a0/time.h>

#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall.h>
#include <time.h>
#include <threads.h>
#include <unistd.h>

#include "atomic.h"
#include "clock.h"
#include "ftx.h"

// TSAN is worth the pain of properly annotating our mutex.

// clang-format off
#if defined(__SANITIZE_THREAD__)
  #define A0_TSAN_ENABLED
#elif defined(__has_feature)
  #if __has_feature(thread_sanitizer)
    #define A0_TSAN_ENABLED
  #endif
#endif
// clang-format on

const unsigned __tsan_mutex_linker_init = 1 << 0;
const unsigned __tsan_mutex_write_reentrant = 1 << 1;
const unsigned __tsan_mutex_read_reentrant = 1 << 2;
const unsigned __tsan_mutex_not_static = 1 << 8;
const unsigned __tsan_mutex_read_lock = 1 << 3;
const unsigned __tsan_mutex_try_lock = 1 << 4;
const unsigned __tsan_mutex_try_lock_failed = 1 << 5;
const unsigned __tsan_mutex_recursive_lock = 1 << 6;
const unsigned __tsan_mutex_recursive_unlock = 1 << 7;

#ifdef A0_TSAN_ENABLED

void __tsan_mutex_create(void* addr, unsigned flags);
void __tsan_mutex_destroy(void* addr, unsigned flags);
void __tsan_mutex_pre_lock(void* addr, unsigned flags);
void __tsan_mutex_post_lock(void* addr, unsigned flags, int recursion);
int __tsan_mutex_pre_unlock(void* addr, unsigned flags);
void __tsan_mutex_post_unlock(void* addr, unsigned flags);
void __tsan_mutex_pre_signal(void* addr, unsigned flags);
void __tsan_mutex_post_signal(void* addr, unsigned flags);
void __tsan_mutex_pre_divert(void* addr, unsigned flags);
void __tsan_mutex_post_divert(void* addr, unsigned flags);

#else

#define _u_ __attribute__((unused))

A0_STATIC_INLINE void _u_ __tsan_mutex_create(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_destroy(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_pre_lock(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_post_lock(_u_ void* addr,
                                                 _u_ unsigned flags,
                                                 _u_ int recursion) {}
A0_STATIC_INLINE int _u_ __tsan_mutex_pre_unlock(_u_ void* addr, _u_ unsigned flags) {
  return 0;
}
A0_STATIC_INLINE void _u_ __tsan_mutex_post_unlock(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_pre_signal(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_post_signal(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_pre_divert(_u_ void* addr, _u_ unsigned flags) {}
A0_STATIC_INLINE void _u_ __tsan_mutex_post_divert(_u_ void* addr, _u_ unsigned flags) {}

#endif

thread_local bool _a0_robust_init = false;

A0_STATIC_INLINE
void a0_robust_reset() {
  _a0_robust_init = 0;
}

A0_STATIC_INLINE
void a0_robust_reset_atfork() {
  pthread_atfork(NULL, NULL, &a0_robust_reset);
}

static pthread_once_t a0_robust_reset_atfork_once;

typedef struct robust_list robust_list_t;
typedef struct robust_list_head robust_list_head_t;

thread_local robust_list_head_t _a0_robust_head;

A0_STATIC_INLINE
void robust_init() {
  _a0_robust_head.list.next = &_a0_robust_head.list;
  _a0_robust_head.futex_offset = offsetof(a0_mtx_t, ftx);
  _a0_robust_head.list_op_pending = NULL;
  syscall(SYS_set_robust_list, &_a0_robust_head.list, sizeof(_a0_robust_head));
}

A0_STATIC_INLINE
void init_thread() {
  if (_a0_robust_init) {
    return;
  }

  pthread_once(&a0_robust_reset_atfork_once, a0_robust_reset_atfork);
  robust_init();
  _a0_robust_init = true;
}

A0_STATIC_INLINE
void robust_op_start(a0_mtx_t* mtx) {
  init_thread();
  _a0_robust_head.list_op_pending = (struct robust_list*)mtx;
  a0_barrier();
}

A0_STATIC_INLINE
void robust_op_end(a0_mtx_t* mtx) {
  (void)mtx;
  a0_barrier();
  _a0_robust_head.list_op_pending = NULL;
}

A0_STATIC_INLINE
bool robust_is_head(a0_mtx_t* mtx) {
  return mtx == (a0_mtx_t*)&_a0_robust_head;
}

A0_STATIC_INLINE
void robust_op_add(a0_mtx_t* mtx) {
  a0_mtx_t* old_first = (a0_mtx_t*)_a0_robust_head.list.next;

  mtx->prev = (a0_mtx_t*)&_a0_robust_head;
  mtx->next = old_first;

  a0_barrier();

  _a0_robust_head.list.next = (robust_list_t*)mtx;
  if (!robust_is_head(old_first)) {
    old_first->prev = mtx;
  }
}

A0_STATIC_INLINE
void robust_op_del(a0_mtx_t* mtx) {
  a0_mtx_t* prev = mtx->prev;
  a0_mtx_t* next = mtx->next;
  prev->next = next;
  if (!robust_is_head(next)) {
    next->prev = prev;
  }
}

A0_STATIC_INLINE
uint32_t ftx_tid(a0_ftx_t ftx) {
  return ftx & FUTEX_TID_MASK;
}

A0_STATIC_INLINE
bool ftx_owner_died(a0_ftx_t ftx) {
  return ftx & FUTEX_OWNER_DIED;
}

static const uint32_t FTX_NOTRECOVERABLE = FUTEX_TID_MASK | FUTEX_OWNER_DIED;

A0_STATIC_INLINE
bool ftx_notrecoverable(a0_ftx_t ftx) {
  return (ftx & FTX_NOTRECOVERABLE) == FTX_NOTRECOVERABLE;
}

A0_STATIC_INLINE
errno_t a0_mtx_timedlock_robust(a0_mtx_t* mtx, const a0_time_mono_t* timeout) {
  const uint32_t tid = a0_tid();

  errno_t err = EINTR;
  while (err == EINTR) {
    // Can't lock if borked.
    if (ftx_notrecoverable(a0_atomic_load(&mtx->ftx))) {
      return ENOTRECOVERABLE;
    }

    // Try to lock without kernel involvement.
    if (a0_cas(&mtx->ftx, 0, tid)) {
      return A0_OK;
    }

    // Ask the kernel to lock.
    err = a0_ftx_lock_pi(&mtx->ftx, timeout);
  }

  if (!err) {
    return ftx_owner_died(a0_atomic_load(&mtx->ftx)) ? EOWNERDEAD : A0_OK;
  }

  return err;
}

A0_STATIC_INLINE
errno_t a0_mtx_timedlock_impl(a0_mtx_t* mtx, const a0_time_mono_t* timeout) {
  // Note: __tsan_mutex_pre_lock should come here, but tsan doesn't provide
  //       a way to "fail" a lock. Only a trylock.
  robust_op_start(mtx);
  const errno_t err = a0_mtx_timedlock_robust(mtx, timeout);
  if (!err || err == EOWNERDEAD) {
    __tsan_mutex_pre_lock(mtx, 0);
    robust_op_add(mtx);
    __tsan_mutex_post_lock(mtx, 0, 0);
  }
  robust_op_end(mtx);
  return err;
}

errno_t a0_mtx_timedlock(a0_mtx_t* mtx, a0_time_mono_t timeout) {
  return a0_mtx_timedlock_impl(mtx, &timeout);
}

errno_t a0_mtx_lock(a0_mtx_t* mtx) {
  return a0_mtx_timedlock_impl(mtx, NULL);
}

A0_STATIC_INLINE
errno_t a0_mtx_trylock_impl(a0_mtx_t* mtx) {
  const uint32_t tid = a0_tid();

  // Try to lock without kernel involvement.
  uint32_t old = a0_cas_val(&mtx->ftx, 0, tid);

  // Did it work?
  if (!old) {
    robust_op_add(mtx);
    return A0_OK;
  }

  // Is the lock still usable?
  if (ftx_notrecoverable(old)) {
    return ENOTRECOVERABLE;
  }

  // Is the owner still alive?
  if (!ftx_owner_died(old)) {
    return EBUSY;
  }

  // Oh, the owner died. Ask the kernel to fix the state.
  errno_t err = a0_ftx_trylock_pi(&mtx->ftx);
  if (!err) {
    robust_op_add(mtx);
    return ftx_owner_died(a0_atomic_load(&mtx->ftx)) ? EOWNERDEAD : A0_OK;
  }

  // EAGAIN means that somebody else beat us to it.
  // Anything else means we're borked.
  return err == EAGAIN ? EBUSY : ENOTRECOVERABLE;
}

errno_t a0_mtx_trylock(a0_mtx_t* mtx) {
  __tsan_mutex_pre_lock(mtx, __tsan_mutex_try_lock);
  robust_op_start(mtx);
  errno_t err = a0_mtx_trylock_impl(mtx);
  robust_op_end(mtx);
  if (!err || err == EOWNERDEAD) {
    __tsan_mutex_post_lock(mtx, __tsan_mutex_try_lock, 0);
  } else {
    __tsan_mutex_post_lock(mtx, __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 0);
  }
  return err;
}

errno_t a0_mtx_consistent(a0_mtx_t* mtx) {
  const uint32_t val = a0_atomic_load(&mtx->ftx);

  // Why fix what isn't broken?
  if (!ftx_owner_died(val)) {
    return EINVAL;
  }

  // Is it yours to fix?
  if (ftx_tid(val) != a0_tid()) {
    return EPERM;
  }

  // Fix it!
  a0_atomic_and_fetch(&mtx->ftx, ~FUTEX_OWNER_DIED);

  return A0_OK;
}

errno_t a0_mtx_unlock(a0_mtx_t* mtx) {
  const uint32_t tid = a0_tid();

  const uint32_t val = a0_atomic_load(&mtx->ftx);

  // Only the owner can unlock.
  if (ftx_tid(val) != tid) {
    return EPERM;
  }

  __tsan_mutex_pre_unlock(mtx, 0);

  // If the mutex was acquired with EOWNERDEAD, the caller is responsible
  // for fixing the state and marking the mutex consistent. If they did
  // not mark it consistent and are unlocking... then we are unrecoverably
  // borked!
  uint32_t new_val = 0;
  if (ftx_owner_died(val)) {
    new_val = FTX_NOTRECOVERABLE;
  }

  robust_op_start(mtx);
  robust_op_del(mtx);

  // If the futex is exactly equal to tid, then there are no waiters and the
  // kernel doesn't need to get involved.
  if (!a0_cas(&mtx->ftx, tid, new_val)) {
    // Ask the kernel to wake up a waiter.
    a0_ftx_unlock_pi(&mtx->ftx);
    if (new_val) {
      a0_atomic_or_fetch(&mtx->ftx, new_val);
    }
  }

  robust_op_end(mtx);
  __tsan_mutex_post_unlock(mtx, 0);

  return A0_OK;
}

// TODO: Handle ENOTRECOVERABLE
A0_STATIC_INLINE
errno_t a0_cnd_timedwait_impl(a0_cnd_t* cnd, a0_mtx_t* mtx, const a0_time_mono_t* timeout) {
  const uint32_t init_cnd = a0_atomic_load(cnd);

  // Unblock other threads to do the things that will eventually signal this wait.
  errno_t err = a0_mtx_unlock(mtx);
  if (err) {
    return err;
  }

  __tsan_mutex_pre_lock(mtx, 0);
  robust_op_start(mtx);

  do {
    // Priority-inheritance-aware wait until awoken or timeout.
    err = a0_ftx_wait_requeue_pi(cnd, init_cnd, timeout, &mtx->ftx);
  } while (err == EINTR);

  // We need to manually lock on timeout.
  // Note: We keep the timeout error.
  if (err == ETIMEDOUT) {
    a0_mtx_timedlock_robust(mtx, NULL);
  }
  // Someone else grabbed and mutated the resource between the unlock and wait.
  // No need to wait.
  if (err == EAGAIN) {
    err = a0_mtx_timedlock_robust(mtx, NULL);
  }

  robust_op_add(mtx);

  // If no higher priority error, check the previous owner didn't die.
  if (!err) {
    err = ftx_owner_died(a0_atomic_load(&mtx->ftx)) ? EOWNERDEAD : A0_OK;
  }

  robust_op_end(mtx);
  __tsan_mutex_post_lock(mtx, 0, 0);
  return err;
}

errno_t a0_cnd_timedwait(a0_cnd_t* cnd, a0_mtx_t* mtx, a0_time_mono_t timeout) {
  // Let's not unlock the mutex if we're going to get EINVAL due to a bad timeout.
  if ((timeout.ts.tv_sec < 0 || timeout.ts.tv_nsec < 0 || (!timeout.ts.tv_sec && !timeout.ts.tv_nsec) || timeout.ts.tv_nsec >= NS_PER_SEC)) {
    return EINVAL;
  }
  return a0_cnd_timedwait_impl(cnd, mtx, &timeout);
}

errno_t a0_cnd_wait(a0_cnd_t* cnd, a0_mtx_t* mtx) {
  return a0_cnd_timedwait_impl(cnd, mtx, NULL);
}

A0_STATIC_INLINE
errno_t a0_cnd_wake(a0_cnd_t* cnd, a0_mtx_t* mtx, uint32_t cnt) {
  uint32_t val = a0_atomic_add_fetch(cnd, 1);

  while (true) {
    errno_t err = a0_ftx_cmp_requeue_pi(cnd, val, &mtx->ftx, cnt);
    if (err != EAGAIN) {
      return err;
    }

    // Another thread is also trying to wake this condition variable.
    val = a0_atomic_load(cnd);
  }
}

errno_t a0_cnd_signal(a0_cnd_t* cnd, a0_mtx_t* mtx) {
  return a0_cnd_wake(cnd, mtx, 1);
}

errno_t a0_cnd_broadcast(a0_cnd_t* cnd, a0_mtx_t* mtx) {
  return a0_cnd_wake(cnd, mtx, INT_MAX);
}
