/*
 * Copyright (c) 2016 UChicago Argonne, LLC
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(swim)

#include "swim_internal.h"
#include <assert.h>

static const char *SWIM_STATUS_STR[] = {
	[SWIM_MEMBER_ALIVE]	= "ALIVE",
	[SWIM_MEMBER_SUSPECT]	= "SUSPECT",
	[SWIM_MEMBER_DEAD]	= "DEAD",
	[SWIM_MEMBER_INACTIVE]	= "INACTIVE",
};

static uint64_t swim_prot_period_len;  // 1s
static uint64_t swim_suspect_timeout;  // 8s
static uint64_t swim_ping_timeout;     // 900ms

static inline uint64_t
swim_prot_period_len_default(void)
{
	unsigned int val = SWIM_PROTOCOL_PERIOD_LEN;

	d_getenv_int("SWIM_PROTOCOL_PERIOD_LEN", &val);
	return val;
}

static inline uint64_t
swim_suspect_timeout_default(void)
{
	unsigned int val = SWIM_SUSPECT_TIMEOUT;

	d_getenv_int("SWIM_SUSPECT_TIMEOUT", &val);
	return val;
}

static inline uint64_t
swim_ping_timeout_default(void)
{
	unsigned int val = SWIM_PING_TIMEOUT;

	d_getenv_int("SWIM_PING_TIMEOUT", &val);
	return val;
}

void
swim_period_set(uint64_t val)
{
	D_DEBUG(DB_TRACE, "swim_prot_period_len set as %lu\n", val);
	swim_prot_period_len = val;
}

uint64_t
swim_period_get(void)
{
	return swim_prot_period_len;
}

void
swim_suspect_timeout_set(uint64_t val)
{
	D_DEBUG(DB_TRACE, "swim_suspect_timeout set as %lu\n", val);
	swim_suspect_timeout = val;
}

uint64_t
swim_suspect_timeout_get(void)
{
	return swim_suspect_timeout;  // 8s
}

void
swim_ping_timeout_set(uint64_t val)
{
	D_DEBUG(DB_TRACE, "swim_ping_timeout set as %lu\n", val);
	swim_ping_timeout = val;
}

uint64_t
swim_ping_timeout_get(void)
{
	return swim_ping_timeout;
}

static inline void
swim_dump_updates(swim_id_t self_id, swim_id_t from_id, swim_id_t to_id,
		  struct swim_member_update *upds, size_t nupds)
{
	FILE	*fp;
	char	*msg;
	size_t	 msg_size, i;
	int	 rc;

	if (!D_LOG_ENABLED(DLOG_DBG))
		return;

	fp = open_memstream(&msg, &msg_size);
	if (fp != NULL) {
		for (i = 0; i < nupds; i++) {
			rc = fprintf(fp, " {%lu %c %lu}", upds[i].smu_id,
				SWIM_STATUS_CHARS[upds[i].smu_state.sms_status],
				     upds[i].smu_state.sms_incarnation);
			if (rc < 0)
				break;
		}

		fclose(fp);
		/* msg and msg_size will be set after fclose(fp) only */
		if (msg_size > 0)
			SWIM_INFO("%lu %s %lu:%s\n", self_id,
				  self_id == from_id ? "=>" : "<=",
				  self_id == from_id ? to_id : from_id, msg);
		free(msg); /* allocated by open_memstream() */
	}
}

int
swim_updates_prepare(struct swim_context *ctx, swim_id_t id, swim_id_t to,
		     struct swim_member_update **pupds, size_t *pnupds)
{
	struct swim_member_update	*upds;
	struct swim_item		*next, *item;
	swim_id_t			 self_id = swim_self_get(ctx);
	size_t				 nupds, n = 0;
	int				 rc = 0;

	if (id == SWIM_ID_INVALID || to == SWIM_ID_INVALID) {
		SWIM_ERROR("member id is invalid\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	nupds = SWIM_PIGGYBACK_ENTRIES + 1 /* id */;  // 8个辅助 + 一个目标
	if (id != self_id)
		nupds++; /* self_id */  // 再加上自己
	if (id != to) 				// 再加上发送的节点
		nupds++; /* to */

	D_ALLOC_ARRAY(upds, nupds);  // 申请空间
	if (upds == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	swim_ctx_lock(ctx);

    // 第一个槽位填写待查询rank的状态
	rc = ctx->sc_ops->get_member_state(ctx, id, &upds[n].smu_state);  // 获取rank的状态
	if (rc) {
		if (rc == -DER_NONEXIST)
			SWIM_INFO("%lu: not bootstrapped yet with %lu\n", self_id, id);
		else
			SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", id, DP_RC(rc));
		D_GOTO(out_unlock, rc);
	}
	upds[n++].smu_id = id;  // rank的id

    // 第二个槽位填本节点的状态
	if (id != self_id) {
		/* update self status on target */
		rc = ctx->sc_ops->get_member_state(ctx, self_id,
						   &upds[n].smu_state);
		if (rc) {
			SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", self_id,
				   DP_RC(rc));
			D_GOTO(out_unlock, rc);
		}
		upds[n++].smu_id = self_id;
	}

    // 第三个槽位填要发往的rank的信息
	if (id != to) {
		rc = ctx->sc_ops->get_member_state(ctx, to, &upds[n].smu_state);
		if (rc) {
			if (rc == -DER_NONEXIST)
				SWIM_INFO("%lu: not bootstrapped yet with %lu\n", self_id, to);
			else
				SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", to, DP_RC(rc));
			D_GOTO(out_unlock, rc);
		}
		upds[n++].smu_id = to;
	}

    // 遍历查询所有的sc_updates中的成员
    // sc_updates存放的是ping过我和我ping过的rank集合
	item = TAILQ_FIRST(&ctx->sc_updates);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);

		/* delete entries that are too many */
		// sc_updates中缓存了太多了rank了, 就删除掉尾巴上的一些rank
		// 往sc_updates中插入的时候总是插入在头部
		// 所以这里尾巴删掉的是老旧的数据(LRU)
		if (n >= nupds) {
			TAILQ_REMOVE(&ctx->sc_updates, item, si_link);
			D_FREE(item);
			item = next;
			continue;
		}

		/* update with recent updates */
		// 获取不相关的rank的信息
		if (item->si_id != id &&
		    item->si_id != self_id &&
		    item->si_id != to) {
			rc = ctx->sc_ops->get_member_state(ctx, item->si_id,
							   &upds[n].smu_state);
			if (rc) {
				if (rc == -DER_NONEXIST) {
					/* this member was removed already */
					TAILQ_REMOVE(&ctx->sc_updates, item,
						     si_link);
					D_FREE(item);
					item = next;
					continue;
				}
				SWIM_ERROR("get_member_state(%lu): "DF_RC"\n",
					   item->si_id, DP_RC(rc));
				D_GOTO(out_unlock, rc);
			}
			upds[n++].smu_id = item->si_id;
		}

		// 每个item往外扩散50次后就从链表中删除掉
		if (++item->u.si_count > ctx->sc_piggyback_tx_max) {
			TAILQ_REMOVE(&ctx->sc_updates, item, si_link);
			D_FREE(item);
		}

		item = next;
	}
	rc = 0;

out_unlock:
	swim_ctx_unlock(ctx);

	if (rc) {
		D_FREE(upds);
	} else {
		swim_dump_updates(self_id, self_id, to, upds, n);
		*pupds  = upds;
		*pnupds = n;
	}
out:
	return rc;
}

int
swim_updates_send(struct swim_context *ctx, swim_id_t id/*想要查询状态的远端rank*/, swim_id_t to/*本消息发送的远端rank*/)
{
	struct swim_member_update	*upds;
	size_t				         nupds;  // upds数组的大小
	int				 rc;

	rc = swim_updates_prepare(ctx, id, to, &upds, &nupds);
	if (rc)
		goto out;

	rc = ctx->sc_ops->send_request(ctx, id, to, upds, nupds);  // 发送
	if (rc)
		D_FREE(upds);
out:
	return rc;
}

static int
swim_updates_notify(struct swim_context *ctx, swim_id_t from, swim_id_t id,
		    struct swim_member_state *id_state, uint64_t count)
{
	struct swim_item *item;

	/* determine if this member already have an update */
	// 已经有了, 有就更新信息
	TAILQ_FOREACH(item, &ctx->sc_updates, si_link) {
		if (item->si_id == id) {
			item->si_from = from;
			item->u.si_count = count;
			D_GOTO(update, 0);
		}
	}

	/* add this update to recent update list so it will be
	 * piggybacked on future protocol messages
	 */
	D_ALLOC_PTR(item);
	if (item != NULL) {
		item->si_id   = id;
		item->si_from = from;
		item->u.si_count = count;
		TAILQ_INSERT_HEAD(&ctx->sc_updates, item, si_link);   // 新增插入sc_updates
	}
update:
	return ctx->sc_ops->set_member_state(ctx, id, id_state);  // 设置id的状态信息
}

static int
swim_member_alive(struct swim_context *ctx, swim_id_t from/*认为id没问题的rank*/,
		  swim_id_t id/*待标记alive的rank的id*/, uint64_t nr)
{
	struct swim_member_state	 id_state;
	struct swim_item		*item;
	uint64_t			 count = 0;
	int				 rc;

	rc = ctx->sc_ops->get_member_state(ctx, id, &id_state);  // 获取状态
	if (rc) {
		swim_id_t self_id = swim_self_get(ctx);

		if (rc == -DER_NONEXIST)
			SWIM_INFO("%lu: not bootstrapped yet with %lu\n", self_id, id);
		else
			SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", id, DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Do not widely spread the information about bootstrap complete */
	if (id_state.sms_status == SWIM_MEMBER_INACTIVE) {
		count = ctx->sc_piggyback_tx_max;
		D_GOTO(update, rc = 0);
	}

	if (nr > id_state.sms_incarnation)
		D_GOTO(update, rc = 0);

	/* ignore old updates or updates for dead members */
	if (id_state.sms_status == SWIM_MEMBER_DEAD ||
	    id_state.sms_status == SWIM_MEMBER_ALIVE ||
	    id_state.sms_incarnation >= nr)
		D_GOTO(out, rc = -DER_ALREADY);

update:
	/* if member is suspected, remove from suspect list */
	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link) {  // 从suspect队列删除
		if (item->si_id == id) {
			/* remove this member from suspect list */
			TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
			D_FREE(item);
			break;
		}
	}

    // 更新状态
	id_state.sms_incarnation = nr;
	id_state.sms_status = SWIM_MEMBER_ALIVE;
	rc = swim_updates_notify(ctx, from, id, &id_state, count);
out:
	return rc;
}

static int
swim_member_dead(struct swim_context *ctx, swim_id_t from,
		 swim_id_t id, uint64_t nr)
{
	struct swim_member_state	 id_state;
	struct swim_item		*item;
	int				 rc;

	rc = ctx->sc_ops->get_member_state(ctx, id, &id_state);  // 获取待标dead的rank的状态信息
	if (rc) {
		SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", id, DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (id_state.sms_status == SWIM_MEMBER_INACTIVE) {
		if (ctx->sc_glitch)
			D_GOTO(update, rc = 0);
		else
			D_GOTO(out, rc = 0);
	}

	if (nr > id_state.sms_incarnation)
		D_GOTO(update, rc = 0);

	/* ignore old updates or updates for dead members */
	if (id_state.sms_status == SWIM_MEMBER_DEAD ||  // 已经dead了
	    id_state.sms_incarnation > nr)  // 本次的更新请求已经老旧了
		D_GOTO(out, rc = -DER_ALREADY);

update:
	/* if member is suspected, remove it from suspect list */
	// 从suspect队列中移除
	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link) {
		if (item->si_id == id) {
			/* remove this member from suspect list */
			TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
			D_FREE(item);
			break;
		}
	}

	SWIM_ERROR("member %lu is DEAD\n", id);
	id_state.sms_incarnation = nr;
	id_state.sms_status = SWIM_MEMBER_DEAD;  // 标记dead
	rc = swim_updates_notify(ctx, from, id, &id_state, 0);  // 更新状态
out:
	return rc;
}

static int
swim_member_suspect(struct swim_context *ctx, swim_id_t from/*发起者, 认为id的rank有异常*/,
		    swim_id_t id/*被认为有异常的rank*/, uint64_t nr)
{
	struct swim_member_state	 id_state;
	struct swim_item		*item;
	int				 rc;

	/* if there is no suspicion timeout, just kill the member */
	if (swim_suspect_timeout_get() == 0)
		return swim_member_dead(ctx, from, id, nr);  // 如果没有可疑对象的超时监测机制, 直接标记为dead

	rc = ctx->sc_ops->get_member_state(ctx, id, &id_state);  // 获取可疑rank的状态信息
	if (rc) {
		SWIM_ERROR("get_member_state(%lu): "DF_RC"\n", id, DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (id_state.sms_status == SWIM_MEMBER_INACTIVE)  // 未激活
		D_GOTO(out, rc = 0);

	if (nr > id_state.sms_incarnation)
		D_GOTO(search, rc = 0);

	/* ignore old updates or updates for dead members */
	if (id_state.sms_status == SWIM_MEMBER_DEAD ||
	    id_state.sms_status == SWIM_MEMBER_SUSPECT ||
	    id_state.sms_incarnation > nr)
		D_GOTO(out, rc = -DER_ALREADY);

search:
	/* determine if this member is already suspected */
	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link) {  // 可疑链表中能找到这个id的rank
		if (item->si_id == id)
			goto update;  // 直接更新
	}

    // 未找到, 插入

	/* add to end of suspect list */
	D_ALLOC_PTR(item);
	if (item == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	item->si_id   = id;    // 被认为异常的rank
	item->si_from = from;  // 发起者, 认为id的rank有异常
	item->u.si_deadline = swim_now_ms() + swim_suspect_timeout_get();
	TAILQ_INSERT_TAIL(&ctx->sc_suspects, item, si_link);  // 插入suspect链表

update:
	id_state.sms_incarnation = nr;
	id_state.sms_status = SWIM_MEMBER_SUSPECT;
	rc = swim_updates_notify(ctx, from, id, &id_state, 0);
out:
	return rc;
}

static int
swim_member_update_suspected(struct swim_context *ctx, uint64_t now/*当前时间*/,
			     uint64_t net_glitch_delay/*时间漂移(距离本次预期执行时间的偏移)*/)
{
	TAILQ_HEAD(, swim_item)		 targets;
	struct swim_member_state	 id_state;
	struct swim_item		*next, *item;
	swim_id_t			 self_id = swim_self_get(ctx);
	swim_id_t			 from_id, id;
	int				 rc = 0;

	TAILQ_INIT(&targets);  // 形成环状链表

	/* update status of suspected members */
	swim_ctx_lock(ctx);
	item = TAILQ_FIRST(&ctx->sc_suspects);  // sc_suspects链表的第一个元素
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);   // sc_suspects链表中下一个元素的指针
		item->u.si_deadline += net_glitch_delay;  // 由于调度有漂移, deadline也向后移动
		if (now > item->u.si_deadline) {    // 当前时间比deadline要大, 表示deadline已经超期了, 这个超时时间为8秒
			rc = ctx->sc_ops->get_member_state(ctx,
							   item->si_id,
							   &id_state);  // 获取rank的状态
			if (rc || (id_state.sms_status != SWIM_MEMBER_SUSPECT)) {  // 已经不在suspect列表
				/* this member was removed or updated already */
				TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
				D_FREE(item);
				D_GOTO(next_item, rc = 0);
			}

			SWIM_INFO("%lu: suspect timeout %lu\n",
				  self_id, item->si_id);
			if (item->si_from != self_id) {		// 不是本节点首先发现的, 构造item加到targets中，下面会将targets中的东西发出去
				/* let's try to confirm from gossip origin */  // 本节点的suspect状态是其他节点同步过来的，谁同步过来的，我就告诉说
				id      = item->si_id;          // 这个suspect的rank已经超时了，让最开始发现这个rank异常的发现者去处理
				from_id = item->si_from;

				item->si_from = self_id;
				item->u.si_deadline += swim_ping_timeout_get();

				D_ALLOC_PTR(item);
				if (item == NULL)
					D_GOTO(next_item, rc = -DER_NOMEM);
				item->si_id   = id;
				item->si_from = from_id;
				TAILQ_INSERT_TAIL(&targets, item, si_link);
			} else {							// 本节点首先发现的，从suspect队列中删掉, 加到dead队列中
				TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
				/* if this member has exceeded
				 * its allowable suspicion timeout,
				 * we mark it as dead
				 */
				swim_member_dead(ctx, item->si_from,
						 item->si_id,
						 id_state.sms_incarnation);  // 标记dead
				D_FREE(item);
			}
		} else {  // dealine在未来超时
			if (item->u.si_deadline < ctx->sc_next_event)  // deadline在下次执行前超时
				ctx->sc_next_event = item->u.si_deadline;  // 将下一次的执行时间设置为daedline时间
		}
next_item:
		item = next;
	}
	swim_ctx_unlock(ctx);

	/* send confirmations to selected members */
	item = TAILQ_FIRST(&targets);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);

		SWIM_INFO("try to confirm from source. %lu: %lu <= %lu\n",
			  self_id, item->si_id, item->si_from);

		rc = swim_updates_send(ctx, item->si_id, item->si_from);  // si_id的问题不是本节点先发现的，是si_from先发现的
		if (rc)                                                   // 告知si_from
			SWIM_ERROR("swim_updates_send(): "DF_RC"\n", DP_RC(rc));

		D_FREE(item);
		item = next;
	}

	return rc;
}

static int
swim_ipings_update(struct swim_context *ctx, uint64_t now,
		   uint64_t net_glitch_delay)
{
	TAILQ_HEAD(, swim_item)		 targets;
	struct swim_item		*next, *item;
	swim_id_t			 self_id = swim_self_get(ctx);
	int				 rc = 0;

	TAILQ_INIT(&targets);

    // 遍历所有iping的成员, 判断是否超时, 超时的话就加入target队列中
	swim_ctx_lock(ctx);
	item = TAILQ_FIRST(&ctx->sc_ipings);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		item->u.si_deadline += net_glitch_delay;  // deadline + 时间漂移
		if (now > item->u.si_deadline) {
			TAILQ_REMOVE(&ctx->sc_ipings, item, si_link);  // 从iping链表中移除
			TAILQ_INSERT_TAIL(&targets, item, si_link);
		} else {
			if (item->u.si_deadline < ctx->sc_next_event)
				ctx->sc_next_event = item->u.si_deadline;
		}
		item = next;
	}
	swim_ctx_unlock(ctx);

	/* send notification to expired members */
	// 向过期的iping相关rank发送消息
	item = TAILQ_FIRST(&targets);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		SWIM_INFO("reply IREQ expired. %lu: %lu => %lu\n",
			  self_id, item->si_from, item->si_id);

		rc = ctx->sc_ops->send_reply(ctx, item->si_id, item->si_from,
					     -DER_TIMEDOUT, item->si_args);
		if (rc)
			SWIM_ERROR("send_reply(): "DF_RC"\n", DP_RC(rc));

		D_FREE(item);
		item = next;
	}

	return rc;
}

int
swim_ipings_reply(struct swim_context *ctx, swim_id_t to_id, int ret_rc)
{
	TAILQ_HEAD(, swim_item)		 targets;
	struct swim_item		*next, *item;
	swim_id_t			 self_id = swim_self_get(ctx);
	int				 rc = 0;

	TAILQ_INIT(&targets);

	swim_ctx_lock(ctx);
	item = TAILQ_FIRST(&ctx->sc_ipings);  // iping列表中包含to_id
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		if (item->si_id == to_id) {
			TAILQ_REMOVE(&ctx->sc_ipings, item, si_link);  // 收到应答就删掉
			TAILQ_INSERT_TAIL(&targets, item, si_link);
		}
		item = next;
	}
	swim_ctx_unlock(ctx);

	item = TAILQ_FIRST(&targets);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		SWIM_INFO("reply IREQ. %lu: %lu <= %lu\n",
			  self_id, item->si_id, item->si_from);

		rc = ctx->sc_ops->send_reply(ctx, item->si_id, item->si_from,
					     ret_rc, item->si_args);
		if (rc)
			SWIM_ERROR("send_reply(): "DF_RC"\n", DP_RC(rc));

		D_FREE(item);
		item = next;
	}

	return rc;
}

int
swim_ipings_suspend(struct swim_context *ctx, swim_id_t from_id,
		    swim_id_t to_id, void *args)
{
	struct swim_item	*item;
	int			 rc = 0;

	swim_ctx_lock(ctx);
	/* looking if sent already */
	TAILQ_FOREACH(item, &ctx->sc_ipings, si_link) {
		if (item->si_id == to_id) {
			/* don't ping a second time */
			rc = -DER_ALREADY;
			break;
		}
	}

	D_ALLOC_PTR(item);
	if (item != NULL) {
		item->si_id   = to_id;    // iping要查询的rank
		item->si_from = from_id;  // 原始节点，发送iping消息的rank
		item->si_args = args;     // 原始节点发过来消息的rpc句柄
		item->u.si_deadline = swim_now_ms() + swim_ping_timeout_get();
		TAILQ_INSERT_TAIL(&ctx->sc_ipings, item, si_link);
	} else {
		rc = -DER_NOMEM;
	}
	swim_ctx_unlock(ctx);

	return rc;
}

static int
swim_subgroup_init(struct swim_context *ctx)
{
	struct swim_item	*item;
	swim_id_t		 id;
	int			 i, rc = 0;

	for (i = 0; i < SWIM_SUBGROUP_SIZE; i++) {
		id = ctx->sc_ops->get_iping_target(ctx);
		if (id == SWIM_ID_INVALID)
			D_GOTO(out, rc = 0);

		D_ALLOC_PTR(item);
		if (item == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		item->si_from = ctx->sc_target;  // 想要知道状态的远端rank
		item->si_id   = id;  // 代替我进行dping的远端rank, 由这个rank替我dping一次sc_target
		TAILQ_INSERT_TAIL(&ctx->sc_subgroup, item, si_link);
	}
out:
	return rc;
}

int
swim_member_new_remote(struct swim_context *ctx, swim_id_t id)
{
	struct swim_item	*item;
	int			 rc = 0;

	D_ALLOC_PTR(item);
	if (item == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	swim_ctx_lock(ctx);
	if (swim_state_get(ctx) == SCS_BEGIN) {
		item->si_from = id;
		item->si_id   = id;
		TAILQ_INSERT_TAIL(&ctx->sc_subgroup, item, si_link);
	} else {
		rc = -DER_BUSY;
	}
	swim_ctx_unlock(ctx);

	if (rc)
		D_FREE(item);
out:
	SWIM_INFO("%lu: new remote %lu "DF_RC"\n", swim_self_get(ctx), id, DP_RC(rc));
	return rc;
}

void *
swim_data(struct swim_context *ctx)
{
	return ctx ? ctx->sc_data : NULL;
}

swim_id_t
swim_self_get(struct swim_context *ctx)
{
	return ctx ? ctx->sc_self : SWIM_ID_INVALID;
}

void
swim_self_set(struct swim_context *ctx, swim_id_t self_id)
{
	if (ctx != NULL)
		ctx->sc_self = self_id;
}

struct swim_context *
swim_init(swim_id_t self_id, struct swim_ops *swim_ops, void *data)
{
	struct swim_context	*ctx;
	int			 rc;

	if (swim_ops == NULL ||
	    swim_ops->send_request == NULL ||
	    swim_ops->get_dping_target == NULL ||
	    swim_ops->get_iping_target == NULL ||
	    swim_ops->get_member_state == NULL ||
	    swim_ops->set_member_state == NULL) {
		SWIM_ERROR("there are no proper callbacks specified\n");
		D_GOTO(out, ctx = NULL);
	}

	/* allocate structure for storing swim context */
	D_ALLOC_PTR(ctx);  // 申请空间
	if (ctx == NULL)
		D_GOTO(out, ctx = NULL);

	rc = SWIM_MUTEX_CREATE(ctx->sc_mutex, NULL);  // 锁初始化
	if (rc != 0) {
		D_FREE(ctx);
		D_DEBUG(DB_TRACE, "SWIM_MUTEX_CREATE(): %s\n", strerror(rc));
		D_GOTO(out, ctx = NULL);
	}

	ctx->sc_self = self_id;
	ctx->sc_data = data;
	ctx->sc_ops  = swim_ops;

	TAILQ_INIT(&ctx->sc_subgroup);
	TAILQ_INIT(&ctx->sc_suspects);
	TAILQ_INIT(&ctx->sc_updates);
	TAILQ_INIT(&ctx->sc_ipings);

	/* this can be tuned according members count */
	ctx->sc_piggyback_tx_max = SWIM_PIGGYBACK_TX_COUNT;
	/* force to choose next target first */
	ctx->sc_target = SWIM_ID_INVALID;

	/* delay the first ping until all things will be initialized */
	ctx->sc_next_tick_time = swim_now_ms() + 3 * SWIM_PROTOCOL_PERIOD_LEN;

	/* set global tunable defaults */
	swim_prot_period_len = swim_prot_period_len_default();
	swim_suspect_timeout = swim_suspect_timeout_default();
	swim_ping_timeout    = swim_ping_timeout_default();

	ctx->sc_default_ping_timeout = swim_ping_timeout;

out:
	return ctx;
}

void
swim_fini(struct swim_context *ctx)
{
	struct swim_item	*next, *item;
	int			 rc;

	if (ctx == NULL)
		return;

	swim_ipings_update(ctx, UINT64_MAX, 0);
	item = TAILQ_FIRST(&ctx->sc_ipings);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		TAILQ_REMOVE(&ctx->sc_ipings, item, si_link);
		D_FREE(item);
		item = next;
	}

	item = TAILQ_FIRST(&ctx->sc_updates);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		TAILQ_REMOVE(&ctx->sc_updates, item, si_link);
		D_FREE(item);
		item = next;
	}

	item = TAILQ_FIRST(&ctx->sc_suspects);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		TAILQ_REMOVE(&ctx->sc_suspects, item, si_link);
		D_FREE(item);
		item = next;
	}

	item = TAILQ_FIRST(&ctx->sc_subgroup);
	while (item != NULL) {
		next = TAILQ_NEXT(item, si_link);
		TAILQ_REMOVE(&ctx->sc_subgroup, item, si_link);
		D_FREE(item);
		item = next;
	}

	rc = SWIM_MUTEX_DESTROY(ctx->sc_mutex);
	if (rc != 0)
		D_DEBUG(DB_TRACE, "SWIM_MUTEX_DESTROY(): %s\n", strerror(rc));

	D_FREE(ctx);
}

// 根据网络时延将dealine做释放迁移
int
swim_net_glitch_update(struct swim_context *ctx, swim_id_t id, uint64_t delay)
{
	struct swim_item	*item;
	swim_id_t		 self_id = swim_self_get(ctx);
	int			 rc = 0;

	swim_ctx_lock(ctx);

	/* update expire time of suspected members */
	TAILQ_FOREACH(item, &ctx->sc_suspects, si_link) {
		if (id == self_id || id == item->si_id)
			item->u.si_deadline += delay;  // 可疑队列的超时时间加上网络时延
	}
	/* update expire time of ipinged members */
	TAILQ_FOREACH(item, &ctx->sc_ipings, si_link) {
		if (id == self_id || id == item->si_id)
			item->u.si_deadline += delay;  // iping的超时时间加上网络时延
	}

	if (id == self_id || id == ctx->sc_target) {
		if (swim_state_get(ctx) == SCS_PINGED)
			ctx->sc_deadline += delay;  // dping的超时时间加上网络时延
	}

	swim_ctx_unlock(ctx);

	if (id != self_id)
		SWIM_ERROR("%lu: A network glitch of %lu with %lu ms delay"
			   " is detected.\n", self_id, id, delay);
	return rc;
}

int
swim_progress(struct swim_context *ctx, int64_t timeout/*值为1000*/)
{
	enum swim_context_state	 ctx_state = SCS_TIMEDOUT;
	struct swim_member_state target_state;
	struct swim_item	*item;
	uint64_t		 now, end = 0;
	uint64_t		 net_glitch_delay = 0UL;
	swim_id_t		 target_id, sendto_id;
	bool			 send_updates = false;
	int			 rc;

	/* validate input parameters */
	if (ctx == NULL) {
		SWIM_ERROR("invalid parameter (ctx is NULL)\n");
		D_GOTO(out_err, rc = -DER_INVAL);
	}

	if (ctx->sc_self == SWIM_ID_INVALID) /* not initialized yet */
		D_GOTO(out_err, rc = 0); /* Ignore this update */

	now = swim_now_ms();  // 相对时间
	if (timeout > 0)  // 
		end = now + timeout;  // 当前时间 + 1000ms, 这里有问题的
	ctx->sc_next_event = now + swim_period_get() / 3;  // swim处理下次执行的时间, 外部用这个时间来做网络上的超时等待

	if (now > ctx->sc_expect_progress_time &&
	    0  != ctx->sc_expect_progress_time) {
		net_glitch_delay = now - ctx->sc_expect_progress_time;  // 这个应该是微调时间的吧？
		SWIM_ERROR("The progress callback was not called for too long: "
			   "%lu ms after expected.\n", net_glitch_delay);  // 表示本次执行比期望晚了多久
	}

	for (; now <= end || ctx_state == SCS_TIMEDOUT; now = swim_now_ms()) {
		// suspect列表的处理: suspect超时判断
		rc = swim_member_update_suspected(ctx, now, net_glitch_delay/*与期望执行时间的差值, 漂移的落后时间*/);
		if (rc) {
			SWIM_ERROR("swim_member_update_suspected(): "DF_RC"\n",
				   DP_RC(rc));
			D_GOTO(out, rc);
		}

        // 判断iping的rank是否过期, 过期的话向相关target发送消息
		rc = swim_ipings_update(ctx, now, net_glitch_delay);
		if (rc) {
			SWIM_ERROR("swim_ipings_update(): "DF_RC"\n",
				   DP_RC(rc));
			D_GOTO(out, rc);
		}

		swim_ctx_lock(ctx);
		ctx_state = SCS_SELECT;
		if (ctx->sc_target != SWIM_ID_INVALID) {  // dping对象已知, 第一次时dping对象未知
			rc = ctx->sc_ops->get_member_state(ctx, ctx->sc_target,
							   &target_state);    // 获取dping对象的swim状态信息
			if (rc) {
				ctx->sc_target = SWIM_ID_INVALID;
				if (rc != -DER_NONEXIST) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("get_member_state(%lu): "
						   DF_RC"\n", ctx->sc_target,
						   DP_RC(rc));
					D_GOTO(out, rc);
				}
			} else {
				ctx_state = swim_state_get(ctx);
			}
		}

		switch (ctx_state) {
		case SCS_BEGIN:
			if (now > ctx->sc_next_tick_time) {  // 当前时间比预期的下次执行时间大, 说明可以开始下一次执行了
				if (TAILQ_EMPTY(&ctx->sc_subgroup)) {  // 远端rank集合不为空
					uint64_t delay = target_state.sms_delay * 2;  // 默认的超时时间是网络收发时延的2倍(往返各一次)
					uint64_t ping_timeout = swim_ping_timeout_get();

					// 保证了delay在: 0.9 ~ 2.7
					if (delay < ping_timeout ||
					    delay > 3 * ping_timeout)
						delay = ping_timeout;

					target_id = ctx->sc_target;  // dping的rank
					sendto_id = ctx->sc_target;  // dping的rank
					send_updates = true;
					SWIM_INFO("%lu: dping %lu => {%lu %c %lu} "
						  "delay: %u ms, timeout: %lu ms\n",
						  ctx->sc_self, ctx->sc_self, sendto_id,
						  SWIM_STATUS_CHARS[target_state.sms_status],
						  target_state.sms_incarnation,
						  target_state.sms_delay, delay);

					ctx->sc_next_tick_time = now + swim_period_get();
					ctx->sc_deadline = now + delay;
					if (ctx->sc_deadline < ctx->sc_next_event)
						ctx->sc_next_event = ctx->sc_deadline;
					ctx_state = SCS_PINGED;  // 切换状态
				} else {
					ctx_state = SCS_TIMEDOUT;
				}
			} else {
				if (ctx->sc_next_tick_time < ctx->sc_next_event)
					ctx->sc_next_event = ctx->sc_next_tick_time;  // 期望的执行时间比下次跑进来的时间小, 调整下次跑进来的时间
			}                                                     // 确保我们预期的执行周期可以满足
			break;
		case SCS_PINGED:
			/* check whether the ping target from the previous
			 * protocol tick ever successfully acked a direct
			 * ping request
			 */
			ctx->sc_deadline += net_glitch_delay;  // 超时时间加上调度漂移
			if (now > ctx->sc_deadline) {  // dping已经超时了
				/* no response from direct ping */
				if (target_state.sms_status != SWIM_MEMBER_INACTIVE) {  // dping对象状态不为: 未激活
					/* suspect this member */
					swim_member_suspect(ctx, ctx->sc_self,
							    ctx->sc_target,
						  target_state.sms_incarnation);  // 标记可疑
					ctx_state = SCS_TIMEDOUT;  // 状态切换到超时
				} else {
					/* just goto next member,
					 * this is not ready yet.
					 */
					ctx_state = SCS_SELECT;
				}
				ctx->sc_next_event = now;
			} else {
				if (ctx->sc_deadline < ctx->sc_next_event)  // 上次的dping还未超时
					ctx->sc_next_event = ctx->sc_deadline;  // 下次执行时间设置为dping的超时时间
			}
			break;
		case SCS_TIMEDOUT:
			/* if we don't hear back from the target after an RTT,
			 * kick off a set of indirect pings to a subgroup of
			 * group members
			 */
			item = TAILQ_FIRST(&ctx->sc_subgroup);  
			if (item == NULL) {
				rc = swim_subgroup_init(ctx);  // 找两个iping的rank
				if (rc) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("swim_subgroup_init(): "
						   DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
				item = TAILQ_FIRST(&ctx->sc_subgroup);
			}

            // 存在远端的rank
			if (item != NULL) {
				struct swim_member_state state;

				target_id = item->si_from;  // 代ping的对象, 本节点想要知道状态的远端rank
				sendto_id = item->si_id;    // 远端的rank，由这个远端的rank代替本节点进行dping, dping对象为si_from
				TAILQ_REMOVE(&ctx->sc_subgroup, item, si_link);  // 发完就删掉了？意思每个rank只发一次iping
				D_FREE(item);

				rc = ctx->sc_ops->get_member_state(ctx, sendto_id, &state);
				if (rc) {
					SWIM_ERROR("get_member_state(%lu): "DF_RC"\n",
						   sendto_id, DP_RC(rc));
					goto done_item;
				}
				
				// iping消息发往的对象要求是alive状态的rank
				if (target_id != sendto_id) {
					/* Send indirect ping request to ALIVE member only */
					if (state.sms_status != SWIM_MEMBER_ALIVE)
						goto done_item;

					SWIM_INFO("%lu: ireq  %lu => {%lu %c %lu} "
						  "delay: %u ms\n",
						  ctx->sc_self, sendto_id, target_id,
						  SWIM_STATUS_CHARS[target_state.sms_status],
								    target_state.sms_incarnation,
								    target_state.sms_delay);
				} else {
					/* Send ping only if this member is not respond yet */
					if (state.sms_status != SWIM_MEMBER_INACTIVE)
						goto done_item;

					SWIM_INFO("%lu: dping  %lu => {%lu %c %lu} "
						  "delay: %u ms\n",
						  ctx->sc_self, ctx->sc_self, sendto_id,
						  SWIM_STATUS_CHARS[state.sms_status],
								    state.sms_incarnation,
								    state.sms_delay);
				}

				send_updates = true;
done_item:
				if (TAILQ_EMPTY(&ctx->sc_subgroup)) {  // 发送两个iping对象后，就切换状态发送dping
					/* So, just goto next member. */
					ctx_state = SCS_SELECT;
				}
				break;
			}
			/* fall through to select a next target */
		case SCS_SELECT:
			ctx->sc_target = ctx->sc_ops->get_dping_target(ctx);  // 找到一个非自己非dead的rank的id, 作为dping的对象
			if (ctx->sc_target == SWIM_ID_INVALID) {
				swim_ctx_unlock(ctx);
				D_GOTO(out, rc = -DER_SHUTDOWN);
			}

			if (ctx->sc_next_tick_time < ctx->sc_next_event)
				ctx->sc_next_event = ctx->sc_next_tick_time;
			ctx_state = SCS_BEGIN;  // 设置状态
			break;
		}

		net_glitch_delay = 0UL;
		swim_state_set(ctx, ctx_state);  // 保存状态
		swim_ctx_unlock(ctx);

		if (send_updates) {
			rc = swim_updates_send(ctx, target_id, sendto_id);  // 发送消息：携带的是rank的状态信息
			if (rc) {
				SWIM_ERROR("swim_updates_send(): "
					   DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
			send_updates = false;
		} else if (now + 100 < ctx->sc_next_event) {
			break;
		}
	}
	rc = (now > end) ? -DER_TIMEDOUT : -DER_CANCELED;
out:
	ctx->sc_expect_progress_time = now + swim_period_get() / 2;  // 期望的下次执行时间
out_err:
	return rc;
}

// 更新本节点维护的其他rank的状态
// 本节点自己的状态是不会修改的, 相反增加了自己状态的incarnation
int
swim_updates_parse(struct swim_context *ctx, swim_id_t from_id/*发送消息的对端rank的id*/,
		   struct swim_member_update *upds, size_t nupds)
{
	enum swim_context_state ctx_state;
	struct swim_member_state self_state;
	swim_id_t self_id = swim_self_get(ctx);
	swim_id_t id;
	size_t i;
	int rc = 0;

	swim_dump_updates(self_id, from_id, self_id, upds, nupds);

	if (self_id == SWIM_ID_INVALID || nupds == 0) /* not initialized yet */
		return 0; /* Ignore this update */

	swim_ctx_lock(ctx);
	ctx_state = swim_state_get(ctx);

	if (from_id == ctx->sc_target &&  // dping的消息解析
	    (ctx_state == SCS_BEGIN || ctx_state == SCS_PINGED))
		ctx_state = SCS_SELECT;

	for (i = 0; i < nupds; i++) {
		id = upds[i].smu_id;

		switch (upds[i].smu_state.sms_status) {
		case SWIM_MEMBER_INACTIVE:
			/* ignore inactive updates.
			 * inactive status is only for bootstrapping now,
			 * so it should not be spread across others.
			 */
			break;
		case SWIM_MEMBER_ALIVE:
			if (id == self_id)
				break; /* ignore alive updates for self */

			swim_member_alive(ctx, from_id, id,
					  upds[i].smu_state.sms_incarnation);
			break;
		case SWIM_MEMBER_SUSPECT:
		case SWIM_MEMBER_DEAD:
			if (id == self_id) {  // 远端标记我的状态
				/* increment our incarnation number if we are
				 * suspected/confirmed in the current
				 * incarnation
				 */
				rc = ctx->sc_ops->get_member_state(ctx, self_id,
								   &self_state);  // 获取自己状态
				if (rc) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("get_member_state(%lu): "
						   DF_RC"\n", self_id,
						   DP_RC(rc));
					D_GOTO(out, rc);
				}

				if (self_state.sms_incarnation >
				    upds[i].smu_state.sms_incarnation)
					break; /* already incremented */

				SWIM_ERROR("{%lu %c %lu} self %s received "
					   "{%lu %c %lu} from %lu\n",
					   self_id,
					   SWIM_STATUS_CHARS[
							 self_state.sms_status],
					   self_state.sms_incarnation,
					   SWIM_STATUS_STR[
						  upds[i].smu_state.sms_status],
					   self_id,
					   SWIM_STATUS_CHARS[
						  upds[i].smu_state.sms_status],
					   upds[i].smu_state.sms_incarnation,
					   from_id);

				ctx->sc_ops->new_incarnation(ctx, self_id, &self_state);
				rc = swim_updates_notify(ctx, self_id, self_id,
							 &self_state, 0);  // 更新自己的incarnation
				if (rc) {
					swim_ctx_unlock(ctx);
					SWIM_ERROR("swim_updates_notify(): "
						   DF_RC"\n", DP_RC(rc));
					D_GOTO(out, rc);
				}
				break;
			}

			// 更新其他rank的状态
			if (upds[i].smu_state.sms_status == SWIM_MEMBER_SUSPECT)
				swim_member_suspect(ctx, from_id, id,
					     upds[i].smu_state.sms_incarnation);  // 标记suspect
			else
				swim_member_dead(ctx, from_id, id,
					     upds[i].smu_state.sms_incarnation);  // 标记dead
			break;
		}
	}
	swim_state_set(ctx, ctx_state);
	swim_ctx_unlock(ctx);
out:
	return rc;
}
