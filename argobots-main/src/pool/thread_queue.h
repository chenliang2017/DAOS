/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef THREAD_QUEUE_H_INCLUDED
#define THREAD_QUEUE_H_INCLUDED

#include "abti.h"

// 工作单元队列
/* Generic queue implementation for work units. */
typedef struct {
    size_t num_threads;		// 队列中ABTI_thread的数量
    ABTI_thread *p_head;    // 双向链表的头部
    ABTI_thread *p_tail;	// 双向链表的尾部
    /* If the pool is empty, pop() accesses only is_empty so that pop() does not
     * slow down a push operation. */
    ABTD_atomic_int is_empty; /* Whether the pool is empty or not. 工作队列是否为空 */
} thread_queue_t;

static inline void thread_queue_init(thread_queue_t *p_queue)
{
    p_queue->num_threads = 0;
    p_queue->p_head = NULL;
    p_queue->p_tail = NULL;
    ABTD_atomic_relaxed_store_int(&p_queue->is_empty, 1);  // 置空
}

static inline void thread_queue_free(thread_queue_t *p_queue)
{
    ; /* Do nothing. */
}

// p_queue不为空的情况下, 自旋尝试拿锁
// 拿锁成功时返回0, 失败时返回1
ABTU_ret_err static inline int
thread_queue_acquire_spinlock_if_not_empty(thread_queue_t *p_queue,
                                           ABTD_spinlock *p_lock)
{
    // 判断队列是否为空
    if (ABTD_atomic_acquire_load_int(&p_queue->is_empty)) {  // 队列为空
        /* The pool is empty.  Lock is not taken. */
        return 1; // 队列为空, 直接返回了, 也不用拿锁了
    }

	// 走到这里, 队列不为空
    while (ABTD_spinlock_try_acquire(p_lock)) {  // 拿锁失败
        /* Lock acquisition failed.  Check the size. */
        while (1) {
            if (ABTD_atomic_acquire_load_int(&p_queue->is_empty)) {  // 再次判断队列是否为空
                /* The pool becomes empty.  Lock is not taken. */
                return 1;
            } else if (!ABTD_spinlock_is_locked(p_lock)) {  // p_lock没有加锁, 就再次尝试加锁
                /* Lock seems released.  Let's try to take a lock again. */
                break;
            }
        }
    }

	// 队列不为空, 拿锁成功
    /* Lock is acquired. */
    return 0;
}

// 判断队列是否为空
// 队列为空返回ABT_TRUE, 不为空返回ABT_FALSE
static inline ABT_bool thread_queue_is_empty(const thread_queue_t *p_queue)
{
    return ABTD_atomic_acquire_load_int(&p_queue->is_empty) ? ABT_TRUE
                                                            : ABT_FALSE;
}

// 返回队列的大小
static inline size_t thread_queue_get_size(const thread_queue_t *p_queue)
{
    return p_queue->num_threads;
}

// 插入在队列头部
static inline void thread_queue_push_head(thread_queue_t *p_queue,
                                          ABTI_thread *p_thread)
{
    if (p_queue->num_threads == 0) {
        p_thread->p_prev = p_thread;
        p_thread->p_next = p_thread;
        p_queue->p_head = p_thread;
        p_queue->p_tail = p_thread;
        p_queue->num_threads = 1;
        ABTD_atomic_release_store_int(&p_queue->is_empty, 0);
    } else {
        ABTI_thread *p_head = p_queue->p_head;
        ABTI_thread *p_tail = p_queue->p_tail;
        p_tail->p_next = p_thread;
        p_head->p_prev = p_thread;
        p_thread->p_prev = p_tail;
        p_thread->p_next = p_head;
        p_queue->p_head = p_thread;
        p_queue->num_threads++;
    }
    ABTD_atomic_release_store_int(&p_thread->is_in_pool, 1);
}

// 插入在队列头部
static inline void thread_queue_push_tail(thread_queue_t *p_queue,
                                          ABTI_thread *p_thread)
{
    if (p_queue->num_threads == 0) {
        p_thread->p_prev = p_thread;
        p_thread->p_next = p_thread;
        p_queue->p_head = p_thread;
        p_queue->p_tail = p_thread;
        p_queue->num_threads = 1;
        ABTD_atomic_release_store_int(&p_queue->is_empty, 0);
    } else {
        ABTI_thread *p_head = p_queue->p_head;
        ABTI_thread *p_tail = p_queue->p_tail;
        p_tail->p_next = p_thread;
        p_head->p_prev = p_thread;
        p_thread->p_prev = p_tail;
        p_thread->p_next = p_head;
        p_queue->p_tail = p_thread;
        p_queue->num_threads++;
    }
    ABTD_atomic_release_store_int(&p_thread->is_in_pool, 1);
}

// 工作队列的头部弹出任务
static inline ABTI_thread *thread_queue_pop_head(thread_queue_t *p_queue)
{
    if (p_queue->num_threads > 0) {
        ABTI_thread *p_thread = p_queue->p_head;
        if (p_queue->num_threads == 1) {
            p_queue->p_head = NULL;
            p_queue->p_tail = NULL;
            p_queue->num_threads = 0;
            ABTD_atomic_release_store_int(&p_queue->is_empty, 1);
        } else {
            p_thread->p_prev->p_next = p_thread->p_next;
            p_thread->p_next->p_prev = p_thread->p_prev;
            p_queue->p_head = p_thread->p_next;
            p_queue->num_threads--;
        }

        p_thread->p_prev = NULL;
        p_thread->p_next = NULL;
        ABTD_atomic_release_store_int(&p_thread->is_in_pool, 0);
        return p_thread;
    } else {
        return NULL;
    }
}

// 工作队列的尾部弹出任务
static inline ABTI_thread *thread_queue_pop_tail(thread_queue_t *p_queue)
{
    if (p_queue->num_threads > 0) {
        ABTI_thread *p_thread = p_queue->p_tail;
        if (p_queue->num_threads == 1) {
            p_queue->p_head = NULL;
            p_queue->p_tail = NULL;
            p_queue->num_threads = 0;
            ABTD_atomic_release_store_int(&p_queue->is_empty, 1);
        } else {
            p_thread->p_prev->p_next = p_thread->p_next;
            p_thread->p_next->p_prev = p_thread->p_prev;
            p_queue->p_tail = p_thread->p_prev;
            p_queue->num_threads--;
        }

        p_thread->p_prev = NULL;
        p_thread->p_next = NULL;
        ABTD_atomic_release_store_int(&p_thread->is_in_pool, 0);
        return p_thread;
    } else {
        return NULL;
    }
}

// 从队列中删除p_thread任务
ABTU_ret_err static inline int thread_queue_remove(thread_queue_t *p_queue,
                                                   ABTI_thread *p_thread)
{
    ABTI_CHECK_TRUE(p_queue->num_threads != 0, ABT_ERR_POOL);
    ABTI_CHECK_TRUE(ABTD_atomic_acquire_load_int(&p_thread->is_in_pool) == 1,
                    ABT_ERR_POOL);

    if (p_queue->num_threads == 1) {
        p_queue->p_head = NULL;
        p_queue->p_tail = NULL;
        p_queue->num_threads = 0;
        ABTD_atomic_release_store_int(&p_queue->is_empty, 1);
    } else {
        p_thread->p_prev->p_next = p_thread->p_next;
        p_thread->p_next->p_prev = p_thread->p_prev;
        if (p_thread == p_queue->p_head) {
            p_queue->p_head = p_thread->p_next;
        } else if (p_thread == p_queue->p_tail) {
            p_queue->p_tail = p_thread->p_prev;
        }
        p_queue->num_threads--;
    }
    ABTD_atomic_release_store_int(&p_thread->is_in_pool, 0);
    p_thread->p_prev = NULL;
    p_thread->p_next = NULL;
    return ABT_SUCCESS;
}

// 遍历打印队列中的所有任务
static inline void thread_queue_print_all(const thread_queue_t *p_queue,
                                          void *arg,
                                          void (*print_fn)(void *, ABT_thread))
{
    size_t num_threads = p_queue->num_threads;
    ABTI_thread *p_thread = p_queue->p_head;
    while (num_threads--) {
        ABTI_ASSERT(p_thread);
        ABT_thread thread = ABTI_thread_get_handle(p_thread);
        print_fn(arg, thread);
        p_thread = p_thread->p_next;
    }
}

#endif /* THREAD_QUEUE_H_INCLUDED */
