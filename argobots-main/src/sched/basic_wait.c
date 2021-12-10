/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

static int sched_init(ABT_sched sched, ABT_sched_config config);
static void sched_run(ABT_sched sched);
static int sched_free(ABT_sched);
static void sched_sort_pools(int num_pools, ABT_pool *pools);

static ABT_sched_def sched_basic_wait_def = {
    .type = ABT_SCHED_TYPE_ULT,
    .init = sched_init,
    .run  = sched_run,
    .free = sched_free,
    .get_migr_pool = NULL,
};

typedef struct {
    uint32_t event_freq;	//
    int num_pools;			// 调度器绑定的池数量
    ABT_pool *pools;		// 调度器绑定的所有的池
} sched_data;

ABT_sched_def *ABTI_sched_get_basic_wait_def(void)
{
    return &sched_basic_wait_def;
}

static inline sched_data *sched_data_get_ptr(void *data)
{
    return (sched_data *)data;
}

static int sched_init(ABT_sched sched, ABT_sched_config config)
{
    int abt_errno;
    int num_pools;
    ABTI_global *p_global = ABTI_global_get_global();

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);
    ABTI_sched_config *p_config = ABTI_sched_config_get_ptr(config);

    /* Default settings */
    sched_data *p_data;
    abt_errno = ABTU_malloc(sizeof(sched_data), (void **)&p_data);
    ABTI_CHECK_ERROR(abt_errno);

    /* Set the default value by default. */
    p_data->event_freq = p_global->sched_event_freq;
    if (p_config) {
        int event_freq;
        /* Set the variables from config */
        abt_errno = ABTI_sched_config_read(p_config, ABT_sched_basic_freq.idx,
                                           &event_freq);
        if (abt_errno == ABT_SUCCESS) {
            p_data->event_freq = event_freq;
        }
    }

    /* Save the list of pools */
    num_pools = p_sched->num_pools;  // 绑定的池数
    p_data->num_pools = num_pools;	 // 绑定的所有池
    abt_errno =
        ABTU_malloc(num_pools * sizeof(ABT_pool), (void **)&p_data->pools);
    if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
        ABTU_free(p_data);
        ABTI_CHECK_ERROR(abt_errno);
    }
    memcpy(p_data->pools, p_sched->pools, sizeof(ABT_pool) * num_pools);

    /* Sort pools according to their access mode so the scheduler can execute
       work units from the private pools. */
    if (num_pools > 1) {
        sched_sort_pools(num_pools, p_data->pools);
    }

    p_sched->data = p_data;
    return ABT_SUCCESS;
}

static void sched_run(ABT_sched sched)
{
    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_xstream *p_local_xstream =
        ABTI_local_get_xstream(ABTI_local_get_local());
    uint32_t work_count = 0;
    sched_data *p_data;
    uint32_t event_freq;
    int num_pools;
    ABT_pool *pools;
    int i;
    int run_cnt_nowait;

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_ASSERT(p_sched);

    p_data = sched_data_get_ptr(p_sched->data);
    event_freq = p_data->event_freq;
    num_pools = p_data->num_pools;
    pools = p_data->pools;

    while (1) {
        run_cnt_nowait = 0;

        /* Execute one work unit from the scheduler's pool */
		// 遍历调度器绑定的所有的pool
		// 池的顺序：单一出队和入队、单一出队、单一入队
		// 理由：尽量最小化不同ES的竞争
        for (i = 0; i < num_pools; i++) {
            ABT_pool pool = pools[i];
            ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
            /* Pop one work unit */
            ABT_thread thread =
                ABTI_pool_pop(p_pool, ABT_POOL_CONTEXT_OP_POOL_OTHER);  // 从池里弹出一个待执行的任务(默认是从池的任务头部弹出任务, 也就是先入先出)
            if (thread != ABT_THREAD_NULL) {  // 取到了任务							池里没有任务时直接返回, 不会等待
                ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
                ABTI_ythread_schedule(p_global, &p_local_xstream, p_thread);  // 调度执行
                run_cnt_nowait++;
                break;  // 执行一个任务后就跳出了对pool的循环 ---> 每次只执行一个pool中的任务
            }   // 下次执行的时候换一个池继续判断是否有任务待执行, 下一次执行还是跳到pool0, 如果pool0一直有任务, 会导致其他池的任务饿死
        }

        /* Block briefly on pop_wait() if we didn't find work to do in main loop
         * above. */
        // 本轮没有执行到任务, 说明池中没有任务待执行 
        if (!run_cnt_nowait) {
            ABTI_pool *p_pool = ABTI_pool_get_ptr(pools[0]);
            ABT_thread thread;
            if (p_pool->optional_def.p_pop_wait) {  // 如果池定义了p_pop_wait接口
                thread = ABTI_pool_pop_wait(p_pool, 0.1/*等待时间为0.1秒*/,
                                            ABT_POOL_CONTEXT_OP_POOL_OTHER);  // 睡眠等待任务
            } else if (p_pool->deprecated_def.p_pop_timedwait) {  // 如果池定义了p_pop_timedwait接口
                thread =
                    ABTI_pool_pop_timedwait(p_pool, ABTI_get_wtime() + 0.1);  // 睡眠等待任务, 以将来的一个绝对时间点作为醒来的时刻
            } else {
                /* No "wait" pop, so let's use a normal one. */
                thread = ABTI_pool_pop(p_pool, ABT_POOL_CONTEXT_OP_POOL_OTHER);  // 再次尝试从池中弹出一个任务
            }
            if (thread != ABT_THREAD_NULL) {  // 找到了任务
                ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
                ABTI_ythread_schedule(p_global, &p_local_xstream, p_thread);  // 调度执行
                break;  // 继续下一次循环
            }
        }

        /* If run_cnt_nowait is zero, that means that no units were found in
         * first pass through pools and we must have called pop_wait above.  We
         * should check events regardless of work_count in that case for them to
         * be processed in a timely manner. */
        if (!run_cnt_nowait/*没有任务执行*/ || (++work_count >= event_freq)/*执行次数超限制了*/) {
            ABTI_xstream_check_events(p_local_xstream, p_sched);  // 观察一下这个调度器关联的ES是不是已经终止了
            if (ABTI_sched_has_to_stop(p_sched) == ABT_TRUE)
                break;
            work_count = 0;
        }
    }
}

static int sched_free(ABT_sched sched)
{
    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_ASSERT(p_sched);

    sched_data *p_data = sched_data_get_ptr(p_sched->data);
    ABTU_free(p_data->pools);
    ABTU_free(p_data);
    return ABT_SUCCESS;
}

static int pool_get_access_num(ABT_pool *p_pool)
{
    ABT_pool_access access;
    int num = 0;

    access = ABTI_pool_get_ptr(*p_pool)->access;
    switch (access) {
        case ABT_POOL_ACCESS_PRIV:		// 单一ES入队和出队
            num = 0;
            break;
        case ABT_POOL_ACCESS_SPSC:		// 单一ES出队
        case ABT_POOL_ACCESS_MPSC:
            num = 1;
            break;
        case ABT_POOL_ACCESS_SPMC:		// 单一ES入队
        case ABT_POOL_ACCESS_MPMC:
            num = 2;
            break;
        default:
            ABTI_ASSERT(0);
            ABTU_unreachable();
    }

    return num;
}

// 单一入队 > 单一出队 > 单一出队和入队
static int sched_cmp_pools(const void *p1, const void *p2)
{
    int p1_access, p2_access;

    p1_access = pool_get_access_num((ABT_pool *)p1);
    p2_access = pool_get_access_num((ABT_pool *)p2);

    if (p1_access > p2_access) {
        return 1;
    } else if (p1_access < p2_access) {
        return -1;
    } else {
        return 0;
    }
}

// 池：单一出队和入队、单一出队、单一入队
static void sched_sort_pools(int num_pools, ABT_pool *pools)
{
    qsort(pools, num_pools, sizeof(ABT_pool), sched_cmp_pools);
}
