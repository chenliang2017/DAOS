/*
 * Copyright (C) 2013-2020 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#include "example_rpc_engine.h"

#ifndef EXAMPLE_RPC_H
#    define EXAMPLE_RPC_H

/* visible API for example RPC operation */

#ifdef HG_HAS_BOOST

MERCURY_GEN_PROC(my_rpc_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(my_rpc_in_t, ((int32_t)(input_val))((hg_bulk_t)(bulk_handle)))

#else

typedef struct {
    int32_t ret;
} my_rpc_out_t;

typedef struct {
    int32_t input_val;
    hg_bulk_t bulk_handle;
} my_rpc_in_t;

static HG_INLINE hg_return_t
hg_proc_my_rpc_out_t(hg_proc_t proc, void *data)
{
    hg_return_t ret = HG_SUCCESS;
    my_rpc_out_t *struct_data = (my_rpc_out_t *) data;

    ret = hg_proc_hg_int32_t(proc, &struct_data->ret);
    if (ret != HG_SUCCESS)
        return ret;

    return ret;
}

static HG_INLINE hg_return_t
hg_proc_my_rpc_in_t(hg_proc_t proc, void *data)
{
    hg_return_t ret = HG_SUCCESS;
    my_rpc_in_t *struct_data = (my_rpc_in_t *) data;

    ret = hg_proc_hg_int32_t(proc, &struct_data->input_val);
    if (ret != HG_SUCCESS)
        return ret;

    ret = hg_proc_hg_bulk_t(proc, &struct_data->bulk_handle);
    if (ret != HG_SUCCESS)
        return ret;

    return ret;
}

#endif

hg_id_t
my_rpc_register(void);

#endif /* EXAMPLE_RPC_H */
