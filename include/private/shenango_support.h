#pragma once

#ifdef SHENANGO_THREADS

#include <base/lock.h>
#include <base/log.h>
#include <runtime/runtime.h>
#include <runtime/gc.h>


#define DEFINE_MUTEX(name) mutex_t name = { \
       .held = ATOMIC_INIT(0), \
       .waiter_lock = SPINLOCK_INITIALIZER, \
       .waiters = LIST_HEAD_INIT(name.waiters) \
}

#define DEFINE_CONDVAR(name) condvar_t name = { \
       .waiter_lock = SPINLOCK_INITIALIZER, \
       .waiters = LIST_HEAD_INIT(name.waiters) \
}

#undef WARN
#endif