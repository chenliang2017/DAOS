/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

static int sched_init(ABT_sched sched, ABT_sched_config config);
static void sched_run(ABT_sched sched);
static int sched_free(ABT_sched);
static void sched_sort_pools(int num_pools, ABT_pool *pools);

static ABT_sched_def sched_basic_def = {
    .type = ABT_SCHED_TYPE_ULT,
    .init = sched_init,
    .run  = sched_run,
    .free = sched_free,
    .get_migr_pool = NULL,
};

typedef struct {
    uint32_t event_freq;			// 调度器连续调度的次数上限
    int num_pools;					// 调度器绑定的池数量
    ABT_pool *pools;
#ifdef ABT_CONFIG_USE_SCHED_SLEEP   // 该宏默认不开启
    struct timespec sleep_time;
#endif
} sched_data;

ABT_sched_def *ABTI_sched_get_basic_def(void)
{
    return &sched_basic_def;
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

#ifdef ABT_CONFIG_USE_SCHED_SLEEP  // 宏默认关闭
    p_data->sleep_time.tv_sec = 0;
    p_data->sleep_time.tv_nsec = p_global->sched_sleep_nsec;  // 调度器连续调度达到一定次数时, 睡眠让出调度权
#endif

    /* Set the default value by default. */
    p_data->event_freq = p_global->sched_event_freq;  // 连续调度的次数上限
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
    num_pools = p_sched->num_pools;
    p_data->num_pools = num_pools;  // 池数量
    abt_errno =
        ABTU_malloc(num_pools * sizeof(ABT_pool), (void **)&p_data->pools);
    if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
        ABTU_free(p_data);
        ABTI_CHECK_ERROR(abt_errno);
    }
    memcpy(p_data->pools, p_sched->pools, sizeof(ABT_pool) * num_pools);  // 拷贝池

    /* Sort pools according to their access mode so the scheduler can execute
       work units from the private pools. */
    if (num_pools > 1) {
        sched_sort_pools(num_pools, p_data->pools);  // 排序后: 单一入队和出队、单一出队、单一入队
    }	// 这样保证了这个调度器优先执行自己独占的pool, 然后才执行共享的pool

    p_sched->data = p_data;
    return ABT_SUCCESS;
}

static void sched_run(ABT_sched sched)
{
    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream(ABTI_local_get_local());
    ABT_thread thread = ABT_THREAD_NULL;
    uint32_t pop_count = 0;
    sched_data *p_data;
    uint32_t event_freq;
    int num_pools;
    ABT_pool *pools;
    int i;

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_ASSERT(p_sched);

    p_data = sched_data_get_ptr(p_sched->data);
    event_freq = p_data->event_freq;
    num_pools = p_data->num_pools;	// 调度器绑定的池的数量
    pools = p_data->pools;			// 调度器绑定的所有的池

    while (1) {
		// 遍历调度器绑定的所有的pool
		// 池的顺序：单一出队和入队、单一出队、单一入队
		// 理由：尽量最小化不同ES的竞争
        for (i = 0; i < num_pools; i++) {
            ABTI_pool *p_pool = ABTI_pool_get_ptr(pools[i]);
            ++pop_count;
            thread = ABTI_pool_pop(p_pool, ABT_POOL_CONTEXT_OP_POOL_OTHER);  // 从池里弹出一个待执行的任务(默认是从池的任务头部弹出任务, 也就是先入先出)
            if (thread != ABT_THREAD_NULL) {  // 取到了任务							池里没有任务时直接返回, 不会等待
                ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
                ABTI_ythread_schedule(p_global, &p_local_xstream, p_thread);  // 执行该任务
                break;  // 执行一个任务后就跳出了对pool的循环 ---> 每次只执行一个pool中的任务
            }  // 下次执行的时候换一个池继续判断是否有任务待执行, 下一次执行还是跳到pool0, 如果pool0一直有任务, 会导致其他池的任务饿死
        }
        /* if we attempted event_freq pops, check for events */
		// 连续执行的任务数量达到了阈值
        if (pop_count >= event_freq) {
            ABTI_xstream_check_events(p_local_xstream, p_sched);
            if (ABTI_sched_has_to_stop(p_sched) == ABT_TRUE)
                break;
            SCHED_SLEEP(thread != ABT_THREAD_NULL, p_data->sleep_time);  // 睡眠让cpu控制权
            pop_count = 0;
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
        case ABT_POOL_ACCESS_PRIV:  // 单一ES入队和出队
            num = 0;
            break;
        case ABT_POOL_ACCESS_SPSC:	// 单一ES出队
        case ABT_POOL_ACCESS_MPSC:
            num = 1;
            break;
        case ABT_POOL_ACCESS_SPMC:	// 单一ES入队
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
