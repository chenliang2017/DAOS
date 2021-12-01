/*
 * Copyright (c) 2016 UChicago Argonne, LLC
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __SWIM_INTERNAL_H__
#define __SWIM_INTERNAL_H__

#ifdef _USE_ABT_SYNC_
#include <abt.h>
#else
#include <pthread.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/queue.h>
#include <errno.h>
#include <time.h>

#include <cart/swim.h>
#include <gurt/debug.h>
#include <gurt/common.h>

/* Use debug capability from CaRT */
#define SWIM_INFO(fmt, ...)	    D_DEBUG(DLOG_DBG, fmt, ##__VA_ARGS__)
#define SWIM_ERROR(fmt, ...)	D_DEBUG(DLOG_ERR, fmt, ##__VA_ARGS__)

#ifdef _USE_ABT_SYNC_
#define SWIM_MUTEX_T			ABT_mutex
#define SWIM_MUTEX_CREATE(x, y)	ABT_mutex_create(&(x))
#define SWIM_MUTEX_DESTROY(x)	ABT_mutex_destroy(&(x))
#define SWIM_MUTEX_LOCK(x)		ABT_mutex_lock(x)
#define SWIM_MUTEX_UNLOCK(x)	ABT_mutex_unlock(x)
#else  /* _USE_ABT_SYNC_ */
#define SWIM_MUTEX_T			pthread_mutex_t
#define SWIM_MUTEX_CREATE(x, y)	pthread_mutex_init(&(x), (y))
#define SWIM_MUTEX_DESTROY(x)	pthread_mutex_destroy(&(x))
#define SWIM_MUTEX_LOCK(x)		pthread_mutex_lock(&(x))
#define SWIM_MUTEX_UNLOCK(x)	pthread_mutex_unlock(&(x))
#endif /* _USE_ABT_SYNC_ */

#ifdef __cplusplus
extern "C" {
#endif

/** SWIM protocol parameter defaults */
#define SWIM_PROTOCOL_PERIOD_LEN 1000	/* milliseconds */			// 1s
#define SWIM_SUSPECT_TIMEOUT	(8 * SWIM_PROTOCOL_PERIOD_LEN)		// 8s
#define SWIM_PING_TIMEOUT	900	/* milliseconds */					// 900ms
#define SWIM_SUBGROUP_SIZE	2
#define SWIM_PIGGYBACK_ENTRIES	8	/**< count of piggybacked entries */
#define SWIM_PIGGYBACK_TX_COUNT	50	/**< count of transfers each entry
					 * until it be removed from the list of
					 * updates.
					 */

enum swim_context_state {
	SCS_BEGIN = 0,		/**< initial state when next target was already
				 * selected.
				 */
	SCS_PINGED,		/**< the state after dping was sent and we are
				 * waiting for response.
				 */
	SCS_TIMEDOUT,		/**< the state when no dping response was
				 * received and we should select iping targets.
				 */
	SCS_SELECT,		/**< the state to select next target */
};

struct swim_item {
	TAILQ_ENTRY(swim_item)	 si_link;    // 用来串联整个链表的, 展开为结构体, 内涵两个指针：指向前一个和后一个
	swim_id_t		 si_id;       		 // 要查询的rank
	swim_id_t		 si_from;     		 // 这个字段感觉是哪个rank最先发现这个si_id有问题, 就填哪个rank的id
	void			*si_args;
	union {
		uint64_t	 si_deadline; /**< for sc_suspects/sc_ipings */
		uint64_t	 si_count;    /**< for sc_updates */
	} u;
};

/** internal swim context implementation */
struct swim_context {
	SWIM_MUTEX_T		 sc_mutex;	/**< mutex for modifying */

	void				*sc_data;	/**< private data */
	struct swim_ops		*sc_ops;    // siwm支持的函数集

	TAILQ_HEAD(, swim_item)	 sc_subgroup;  // iping的rank集合
	TAILQ_HEAD(, swim_item)	 sc_suspects;  // 可能出问题的rank集合, 链表形式维护
	TAILQ_HEAD(, swim_item)	 sc_updates;   // ping过我和我ping过的rank的集合
	TAILQ_HEAD(, swim_item)	 sc_ipings;    // 本节点iping的rank集合

	enum swim_context_state	 sc_state;
	swim_id_t		 sc_target;            // 本节点dping的rank的id
	swim_id_t		 sc_self;			   // 自己的rank值

	uint64_t		 sc_default_ping_timeout;
	uint64_t		 sc_expect_progress_time;   // 期望的执行时间? 这玩意是定时器吗？
	uint64_t		 sc_next_tick_time;			// 下一次dping的时间(目前间隔1s执行dping一次), 这个是控制dping的周期的
	uint64_t		 sc_next_event;				// swim处理下次执行的时间, 外部用这个时间来做网络上的超时等待, 这个是控制网络阻塞时间的, 用来确保下次swim_progress的时间
	uint64_t		 sc_deadline;				// dping的超时时间, dping的延时允许在:0.9~2.7秒之间

	uint64_t		 sc_piggyback_tx_max;		//

	unsigned int		 sc_glitch:1;
};

static inline int
swim_ctx_lock(struct swim_context *ctx)
{
	int rc;

	rc = SWIM_MUTEX_LOCK(ctx->sc_mutex);
	if (rc != 0)
		SWIM_ERROR("SWIM_MUTEX_LOCK() failed rc=%d\n", rc);

	return rc;
}

static inline int
swim_ctx_unlock(struct swim_context *ctx)
{
	int rc;

	rc = SWIM_MUTEX_UNLOCK(ctx->sc_mutex);
	if (rc != 0)
		SWIM_ERROR("SWIM_MUTEX_UNLOCK() failed rc=%d\n", rc);

	return rc;
}

static inline uint64_t
swim_now_ms(void)
{
	struct timespec now;
	int rc;

	rc = clock_gettime(CLOCK_MONOTONIC, &now);  // 相对时间

	return rc ? 0 : now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static inline enum swim_context_state
swim_state_get(struct swim_context *ctx)
{
	return ctx->sc_state;
}

static inline void
swim_state_set(struct swim_context *ctx, enum swim_context_state state)
{
	if (ctx->sc_state != state)
		ctx->sc_state = state;
}

/**
 * Set the SWIM protocol period of pings in milliseconds.
 *
 * \note It should NOT be less than 3 * SWIM_PING_TIMEOUT
 *
 * \param[in] val	time in milliseconds
 */
void swim_period_set(uint64_t val);

/**
 * Get the current SWIM protocol period of pings in milliseconds.
 *
 * \return		time in milliseconds
 */
uint64_t swim_period_get(void);

/**
 * Set the "suspected" timeout in milliseconds. This is period of time
 * are waiting for dping/iping response from other nodes. We assume the
 * node is DEAD after this period of time.
 *
 * \param[in] val	timeout in milliseconds
 */
void swim_suspect_timeout_set(uint64_t val);

/**
 * Get the current "suspected" timeout in milliseconds.
 *
 * \return		timeout in milliseconds
 */
uint64_t swim_suspect_timeout_get(void);

/**
 * Set the direct ping (dping) timeout in milliseconds. This is period
 * of time are waiting for dping response from other node. We try to
 * indirectly ping a selected node after this period of time.
 *
 * \param[in] val	timeout in milliseconds
 */
void swim_ping_timeout_set(uint64_t val);

/**
 * Get the current "dping" timeout in milliseconds.
 *
 * \return		timeout in milliseconds
 */
uint64_t swim_ping_timeout_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __SWIM_INTERNAL_H__ */
