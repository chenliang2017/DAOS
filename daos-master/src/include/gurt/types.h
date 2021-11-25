/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 * GURT types.
 */

/** @defgroup GURT GURT */
/** @defgroup GURT_LOG Gurt Log */
/** @defgroup GURT_DEBUG Gurt Debug */
#ifndef __GURT_TYPES_H__
#define __GURT_TYPES_H__

#include <uuid/uuid.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <byteswap.h>

/** @addtogroup GURT
 * @{
 */

#if defined(__cplusplus)
extern "C" {
#else
#define d_is_uuid(var)								\
	(__builtin_types_compatible_p(__typeof__(var), uuid_t) ||			\
	 __builtin_types_compatible_p(__typeof__(var), unsigned char *) ||		\
	 __builtin_types_compatible_p(__typeof__(var), const unsigned char *) ||	\
	 __builtin_types_compatible_p(__typeof__(var), const uuid_t))
#define d_is_string(var)						\
	(__builtin_types_compatible_p(__typeof__(var), char *) ||		\
	 __builtin_types_compatible_p(__typeof__(var), const char *) ||	\
	 __builtin_types_compatible_p(__typeof__(var), const char []) ||	\
	 __builtin_types_compatible_p(__typeof__(var), char []))
#endif

#if defined(__has_warning)
#define D_HAS_WARNING(gcc_version, warning)	__has_warning(warning)
#else  /* !defined(__has_warning) */
#define D_HAS_WARNING(gcc_version, warning) ((gcc_version) <= __GNUC__)
#endif /* defined(__has_warning) */

/**
 * hide the dark secret that uuid_t is an array not a structure.
 */
struct d_uuid {
	uuid_t		uuid;
};

/** iovec for memory buffer */
typedef struct {
	/** buffer address */
	void		*iov_buf;		// 指向存储数据地址的指针
	/** buffer length */
	size_t		iov_buf_len;	// 指针指向内存块的长度
	/** data length */
	size_t		iov_len;		// 数据的长度
} d_iov_t;		// 存一块内存数据的结构体

/** Server identification */
typedef uint32_t	d_rank_t;

typedef struct {
	/** list of ranks */
	d_rank_t	*rl_ranks;  // 集群内部rank列表？？？
	/** number of ranks */
	uint32_t	rl_nr;
} d_rank_list_t;

typedef d_rank_list_t	*d_rank_list_ptr_t;

typedef char		*d_string_t;
typedef const char	*d_const_string_t;

/** Scatter/gather list for memory buffers */
typedef struct {
	uint32_t	sg_nr;		// 数组的大小
	uint32_t	sg_nr_out;	// 数组的大小, 区别是啥？这个代表出参？上面代表入参？
	d_iov_t		*sg_iovs;	// 指针数组
} d_sg_list_t;		// 存内存数据的结构体

/**
 * c string buffer
 */
struct d_string_buffer_t {
	/** c string status */
	int	status;
	/** c string size */
	size_t	str_size;
	/** buffer size */
	size_t	buf_size;
	/** c string buffer address */
	char	*str;
};

static inline void
d_iov_set(d_iov_t *iov, void *buf, size_t size)
{
	iov->iov_buf = buf;
	iov->iov_len = iov->iov_buf_len = size;
}

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /* __GURT_TYPES_H__ */
