/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. Hybrid Logical Clock (HLC) implementation.
 */
#include "crt_internal.h"
#include <gurt/common.h>	/* for NSEC_PER_SEC */
#include <gurt/atomic.h>
#include <time.h>

/**
 * HLC timestamp unit (given in the HLC timestamp value for 1 ns) (i.e.,
 * 1/16 ns, offering a 36-year range)
 */
#define CRT_HLC_NSEC 16ULL

/**
 * HLC start time (given in the Unix time for 2021-01-01 00:00:00 +0000 UTC in
 * seconds) (i.e., together with CRT_HLC_NSEC, offering a range of [2021, 2057])
 */
#define CRT_HLC_START_SEC 1609459200ULL  // 北京时间(东八区): 2021-01-01 08:00:00

/** Mask for the 18 logical bits */
#define CRT_HLC_MASK 0x3FFFFULL			 // 十进制最大为: 262143, 16.383微妙

static ATOMIC uint64_t crt_hlc;			 // 原子变量, 全局变量, 进程唯一

/** See crt_hlc_epsilon_set's API doc */
static uint64_t crt_hlc_epsilon = 1ULL * NSEC_PER_SEC * CRT_HLC_NSEC;  // 集群内任意两节点的最大时间差16秒

/** Get local physical time */
static inline uint64_t crt_hlc_localtime_get(void)
{
	struct timespec now;
	uint64_t	pt;
	int		rc;

	rc = clock_gettime(CLOCK_REALTIME, &now);  // 系统时间
	D_ASSERTF(rc == 0, "clock_gettime: %d\n", errno);
	D_ASSERT(now.tv_sec > CRT_HLC_START_SEC);
	pt = ((now.tv_sec - CRT_HLC_START_SEC) * NSEC_PER_SEC + now.tv_nsec) *
	     CRT_HLC_NSEC;  // 距北京时间(东八区): 2021-01-01 08:00:00经过的纳秒数, 乘以16, 最小时间单位就变成了1/16纳秒

	/** Return the most significant 46 bits of time. */
	// 高46位存物理时间(纳秒), 低18位存细化后的时间
	// 只能感知到物理时间差在16384纳秒以上的时差, 前后时间相差在16384纳秒以内的, 认为物理时间一致
	// 低18位作为逻辑时间, 单位位1/16纳秒, 范围[0, 262143], 只能表示16383纳秒
	return pt & ~CRT_HLC_MASK;
}

// 获取hlc时间, 物理时间一致的情况下, 低18位的逻辑时间加1, 否则就使用新获取的物理时间
uint64_t crt_hlc_get(void)
{
	uint64_t pt = crt_hlc_localtime_get();  // 前后时间相差在16384纳秒以内的, 认为物理时间一致
	uint64_t hlc, ret;

	do {
		hlc = crt_hlc;
		ret = (hlc & ~CRT_HLC_MASK) < pt ? pt : (hlc + 1);  // 每次加1, 代表增加1/16纳秒
	} while (!atomic_compare_exchange(&crt_hlc, hlc, ret)); // 交换存储到全局变量中

	return ret;
}

// 从全局的hlc、最新获取的hlc和msg表示的hlc中选择最大的hlc时间, 值存储在hlc_out中返回, 并且更新全局的crt_hlc
// 一句话概括, 找最大的hlc时间
int crt_hlc_get_msg(uint64_t msg, uint64_t *hlc_out, uint64_t *offset)
{
	uint64_t pt = crt_hlc_localtime_get();
	uint64_t hlc, ret, ml = msg & ~CRT_HLC_MASK;  // 取物理时间
	uint64_t off;

	off = ml > pt ? ml - pt : 0;  // msg时间和本地hlc时间的差异

	if (offset != NULL)
		*offset = off;

	if (off > crt_hlc_epsilon)    // 差异超过16秒, 返错
		return -DER_HLC_SYNC;

	do {
		hlc = crt_hlc;
		if ((hlc & ~CRT_HLC_MASK) < ml)        // 全局存储的物理时间比消息中的物理时间小
			ret = ml < pt ? pt : (msg + 1);       // 消息的物理时间比最新获取的物理时间小, 使用最新获取的时间, 否则使用消息的hlc+1
		else if ((hlc & ~CRT_HLC_MASK) < pt)   // 全局存储的物理时间大于等于消息中的物理时间 && 全局存储的物理时间比最新获取的小
			ret = pt;                             // 使用最新获取的时间
		else if (pt <= ml)                     // 全局存储的物理时间大于等于消息中的物理时间 && 全局存储的物理时间大于等于最新获取 && 最新获取的物理时间小于消息的物理时间
			ret = (hlc < msg ? msg : hlc) + 1;    // 全局存储的hlc时间比消息的小的话就用消息的, 否则用全局存储的hlc+1
		else
			ret = hlc + 1;                        // 使用hlc+1
	} while (!atomic_compare_exchange(&crt_hlc, hlc, ret));    // 将最大的hlc时间设置到全局的crt_hlc时间中

	if (hlc_out != NULL)
		*hlc_out = ret;
	return 0;
}

// hlc和纳秒之间的转换
uint64_t crt_hlc2nsec(uint64_t hlc)
{
	return hlc / CRT_HLC_NSEC;
}

// 纳秒和hlc时间的转换
uint64_t crt_nsec2hlc(uint64_t nsec)
{
	return nsec * CRT_HLC_NSEC;
}

// hlc和unix时间戳之间的转换
uint64_t crt_hlc2unixnsec(uint64_t hlc)
{
	return hlc / CRT_HLC_NSEC + CRT_HLC_START_SEC * NSEC_PER_SEC;
}

// unix时间戳和hlc时间之间的转换
uint64_t crt_unixnsec2hlc(uint64_t unixnsec)
{
	uint64_t start = CRT_HLC_START_SEC * NSEC_PER_SEC;

	/*
	 * If the time represented by unixnsec is before the time represented
	 * by CRT_HLC_START_SEC, or after the maximum time representable, then
	 * the conversion is impossible.
	 */
	if (unixnsec < start || unixnsec - start > (uint64_t)-1 / CRT_HLC_NSEC)
		return 0;

	return (unixnsec - start) * CRT_HLC_NSEC;
}

// 设置集群内节点之间的最大时间差
void crt_hlc_epsilon_set(uint64_t epsilon)
{
	crt_hlc_epsilon = (epsilon + CRT_HLC_MASK) & ~CRT_HLC_MASK;
	D_INFO("set maximum system clock offset to "DF_U64" ns\n",
	       crt_hlc_epsilon);
}

// 获取节点之间的最大时间差
uint64_t crt_hlc_epsilon_get(void)
{
	return crt_hlc_epsilon;
}

uint64_t crt_hlc_epsilon_get_bound(uint64_t hlc)
{
	return (hlc + crt_hlc_epsilon) | CRT_HLC_MASK;
}
