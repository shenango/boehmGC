/*
 * Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
 * Copyright (c) 2000-2005 by Hewlett-Packard Company.  All rights reserved.
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose,  provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 */

/*
 * Support code originally for LinuxThreads, the clone()-based kernel
 * thread package for Linux which is included in libc6.
 *
 * This code no doubt makes some assumptions beyond what is
 * guaranteed by the pthread standard, though it now does
 * very little of that.  It now also supports NPTL, and many
 * other Posix thread implementations.  We are trying to merge
 * all flavors of pthread support code into this file.
 */

#include "private/gc_priv.h"


#if defined(SHENANGO_THREADS)
#include "private/shenango_support.h"


DEFINE_MUTEX(GC_allocate_ml);
static DEFINE_MUTEX(mark_mutex);
static DEFINE_CONDVAR(builder_cv);
static DEFINE_CONDVAR(mark_cv);
static int GC_nprocs = 1;
GC_INNER GC_bool GC_in_thread_creation = FALSE;
GC_INNER GC_bool GC_thr_initialized = FALSE;
__thread int GC_dummy_thread_local;

#ifndef GC_ALWAYS_MULTITHREADED
  GC_INNER GC_bool GC_need_to_lock = TRUE;
#endif
GC_INNER volatile GC_bool GC_collecting = FALSE;


GC_INNER void GC_lock(void)
{
    mutex_lock(&GC_allocate_ml);
}

GC_INNER void GC_acquire_mark_lock(void)
{
  mutex_lock(&mark_mutex);
}

GC_INNER void GC_release_mark_lock(void)
{
    mutex_unlock(&mark_mutex);
}

STATIC void GC_wait_builder(void)
{
    condvar_wait(&builder_cv, &mark_mutex);
}

GC_INNER void GC_wait_for_reclaim(void)
{
    GC_acquire_mark_lock();
    while (GC_fl_builder_count > 0) {
        GC_wait_builder();
    }
    GC_release_mark_lock();
}

GC_INNER void GC_notify_all_builder(void)
{
    condvar_broadcast(&builder_cv);
}

GC_INNER void GC_wait_marker(void)
{
    condvar_wait(&mark_cv, &mark_mutex);
}

GC_INNER void GC_notify_all_marker(void)
{
    condvar_broadcast(&mark_cv);
}

GC_INNER void GC_start_world(void)
{
  gc_start_world();
}


GC_INNER void GC_stop_world(void)
{
  gc_stop_world();
}

static word stack_sz_temp;
static void bounds_cb(word bottom, word top)
{
  BUG_ON(bottom < top);
  log_debug("STACK PUSH %p %p", (ptr_t)bottom, (ptr_t)top);
  GC_push_all_stack_sections((ptr_t)top, (ptr_t)bottom, NULL);
  stack_sz_temp += bottom - top;
} 

GC_INNER void GC_push_all_stacks(void)
{
  stack_sz_temp = 0;
  gc_discover_all_stacks(bounds_cb);
  GC_total_stacksize = stack_sz_temp;
  log_debug("total stacksize = %lu", GC_total_stacksize);
}

void GC_push_thread_structures(void) {}

GC_INNER void GC_init_parallel(void) {}


static ptr_t marker_sp[NCPU] = {0};
STATIC void GC_mark_thread(void * id)
{
  word my_mark_no = 0;
  if ((word)id == GC_WORD_MAX) return; /* to prevent a compiler warning */
  marker_sp[(word)id] = GC_approx_sp();

  /* Inform GC_start_mark_threads about completion of marker data init. */
  GC_acquire_mark_lock();
  if (0 == --GC_fl_builder_count) /* count may have a negative value */
    GC_notify_all_builder();

  for (;; ++my_mark_no) {
    /* GC_mark_no is passed only to allow GC_help_marker to terminate   */
    /* promptly.  This is important if it were called from the signal   */
    /* handler or from the GC lock acquisition code.  Under Linux, it's */
    /* not safe to call it from a signal handler, since it uses mutexes */
    /* and condition variables.  Since it is called only here, the      */
    /* argument is unnecessary.                                         */
    if (my_mark_no < GC_mark_no || my_mark_no > GC_mark_no + 2) {
        /* resynchronize if we get far off, e.g. because GC_mark_no     */
        /* wrapped.                                                     */
        my_mark_no = GC_mark_no;
    }
#   ifdef DEBUG_THREADS
      GC_log_printf("Starting mark helper for mark number %lu\n",
                    (unsigned long)my_mark_no);
#   endif
    GC_help_marker(my_mark_no);
  }
}

GC_INNER void GC_start_mark_threads_inner(void)
{
    GC_ASSERT(I_DONT_HOLD_LOCK());
    if (GC_markers_m1 <= 0) return;
    /* Skip if parallel markers disabled or already started. */
    GC_ASSERT(GC_fl_builder_count == 0);
    for (int i = 0; i < GC_markers_m1; i++)
      BUG_ON(thread_spawn(GC_mark_thread, (void *)(uint64_t)i));

    GC_wait_for_markers_init();

}

const char *names[] = {
    "GC_EVENT_START", /* COLLECTION */
    "GC_EVENT_MARK_START",
    "GC_EVENT_MARK_END",
    "GC_EVENT_RECLAIM_START",
    "GC_EVENT_RECLAIM_END",
    "GC_EVENT_END", /* COLLECTION */
    "GC_EVENT_PRE_STOP_WORLD", /* STOPWORLD_BEGIN */
    "GC_EVENT_POST_STOP_WORLD", /* STOPWORLD_END */
    "GC_EVENT_PRE_START_WORLD", /* STARTWORLD_BEGIN */
    "GC_EVENT_POST_START_WORLD", /* STARTWORLD_END */
    "GC_EVENT_THREAD_SUSPENDED",
    "GC_EVENT_THREAD_UNSUSPENDED"
};

void notify(GC_EventType ev)
{
  log_err("%lu: %s", rdtsc(), names[ev]);
}

GC_INNER void GC_thr_init(void)
{

  GC_set_on_collection_event(notify);

  set_need_to_lock();
  GC_ASSERT(I_HOLD_LOCK());
  if (GC_thr_initialized) return;
  GC_thr_initialized = TRUE;

  GC_ASSERT((word)&GC_threads % sizeof(word) == 0);

# ifdef INCLUDE_LINUX_THREAD_DESCR
    /* Explicitly register the region including the address     */
    /* of a thread local variable.  This should include thread  */
    /* locals for the main thread, except for those allocated   */
    /* in response to dlopen calls.                             */
    {
      ptr_t thread_local_addr = (ptr_t)(&GC_dummy_thread_local);
      ptr_t main_thread_start, main_thread_end;
      if (!GC_enclosing_mapping(thread_local_addr, &main_thread_start,
                                &main_thread_end)) {
        ABORT("Failed to find mapping for main thread thread locals");
      } else {
        /* main_thread_start and main_thread_end are initialized.       */
        GC_add_roots_inner(main_thread_start, main_thread_end, FALSE);
      }
    }
# endif
    GC_nprocs = maxks;
    GC_markers_m1 = GC_nprocs - 1;
  GC_COND_LOG_PRINTF("Number of processors = %d\n", GC_nprocs);
# ifdef PARALLEL_MARK
    if (GC_markers_m1 <= 0) {
      /* Disable parallel marking.      */
      GC_parallel = FALSE;
      GC_COND_LOG_PRINTF(
                "Single marker thread, turning off parallel marking\n");
    } else {
      /* Disable true incremental collection, but generational is OK.   */
      GC_time_limit = GC_TIME_UNLIMITED;
      // setup_mark_lock();
    }
# endif
}


GC_INNER void GC_do_blocking_inner(ptr_t data, void * context GC_ATTR_UNUSED)
{
  BUG();
}

#endif
