/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of the DAOS server. It implements thread-local storage
 * (TLS) for DAOS service threads.
 */
#define D_LOGFAC       DD_FAC(server)

#include <pthread.h>
#include "srv_internal.h"

/* The array remember all of registered module keys on one node. */
struct dss_module_key *dss_module_keys[DAOS_MODULE_KEYS_NR] = { NULL };

pthread_mutex_t dss_module_keys_lock = PTHREAD_MUTEX_INITIALIZER;

void
dss_register_key(struct dss_module_key *key)
{
	int i;

	D_MUTEX_LOCK(&dss_module_keys_lock);
	for (i = 0; i < DAOS_MODULE_KEYS_NR; i++) {
		if (dss_module_keys[i] == NULL) {
			dss_module_keys[i] = key;   // key放在全局的数组中
			key->dmk_index = i;			// 将在全局数组中的位置记录到key的dmk_index中, 便于反向查找
			break;
		}
	}
	D_MUTEX_UNLOCK(&dss_module_keys_lock);
	D_ASSERT(i < DAOS_MODULE_KEYS_NR);
}

void
dss_unregister_key(struct dss_module_key *key)
{
	if (key == NULL)
		return;
	D_ASSERT(key->dmk_index >= 0);
	D_ASSERT(key->dmk_index < DAOS_MODULE_KEYS_NR);
	D_MUTEX_LOCK(&dss_module_keys_lock);
	dss_module_keys[key->dmk_index] = NULL;		// 设置为NULL
	D_MUTEX_UNLOCK(&dss_module_keys_lock);
}

/**
 * Init thread context
 *
 * \param[in]dtls	Init the thread context to allocate the
 *                      local thread variable for each module.
 *
 * \retval		0 if initialization succeeds
 * \retval		negative errno if initialization fails
 */
static int
dss_thread_local_storage_init(struct dss_thread_local_storage *dtls,
			      int xs_id, int tgt_id)
{
	int rc = 0;
	int i;

	if (dtls->dtls_values == NULL) {
		D_ALLOC_ARRAY(dtls->dtls_values,
			      (int)ARRAY_SIZE(dss_module_keys));		// 申请数组空间
		if (dtls->dtls_values == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < DAOS_MODULE_KEYS_NR; i++) {
		struct dss_module_key *dmk = dss_module_keys[i];	// 遍历每一个注册的dss_module_key

		if (dmk != NULL && dtls->dtls_tag & dmk->dmk_tags) {
			D_ASSERT(dmk->dmk_init != NULL);
			dtls->dtls_values[i] = dmk->dmk_init(xs_id, tgt_id);	// 赋值, 这里类型应该是 struct dss_module_info
			if (dtls->dtls_values[i] == NULL) {
				rc = -DER_NOMEM;
				break;
			}
		}
	}
	return rc;
}

/**
 * Finish module context
 *
 * \param[in]dtls	Finish the thread context to free the
 *                      local thread variable for each module.
 */
static void
dss_thread_local_storage_fini(struct dss_thread_local_storage *dtls)
{
	int i;

	if (dtls->dtls_values != NULL) {
		for (i = DAOS_MODULE_KEYS_NR - 1; i >= 0; i--) {
			struct dss_module_key *dmk = dss_module_keys[i];

			if (dmk != NULL && dtls->dtls_tag & dmk->dmk_tags) {
				D_ASSERT(dtls->dtls_values[i] != NULL);
				D_ASSERT(dmk->dmk_fini != NULL);
				dmk->dmk_fini(dtls->dtls_values[i]);
			}
		}
	}

	D_FREE(dtls->dtls_values);
}

// 线程变量
pthread_key_t dss_tls_key;

/*
 * Allocate dss_thread_local_storage for a particular thread and
 * store the pointer in a thread-specific value which can be
 * fetched at any time with dss_tls_get().
 */
struct dss_thread_local_storage *
dss_tls_init(int tag, int xs_id, int tgt_id)
{
	struct dss_thread_local_storage *dtls;
	int		 rc;

	D_ALLOC_PTR(dtls);
	if (dtls == NULL)
		return NULL;

	dtls->dtls_tag = tag;
	rc = dss_thread_local_storage_init(dtls, xs_id, tgt_id);
	if (rc != 0) {
		D_FREE(dtls);
		return NULL;
	}

	rc = pthread_setspecific(dss_tls_key, dtls);	// 设置线程私有变量
	if (rc) {
		D_ERROR("failed to initialize tls: %d\n", rc);
		dss_thread_local_storage_fini(dtls);
		D_FREE(dtls);
		return NULL;
	}

	return dtls;
}

/* Free DTC for a particular thread. */
void
dss_tls_fini(struct dss_thread_local_storage *dtls)
{
	dss_thread_local_storage_fini(dtls);
	D_FREE(dtls);
	pthread_setspecific(dss_tls_key, NULL);
}
