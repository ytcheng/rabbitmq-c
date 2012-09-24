/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MIT
 *
 * Portions created by VMware are Copyright (c) 2007-2012 VMware, Inc.
 * All Rights Reserved.
 *
 * Portions created by Tony Garnock-Jones are Copyright (c) 2009-2010
 * VMware, Inc. and Tony Garnock-Jones. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * ***** END LICENSE BLOCK *****
 */

#ifndef AMQP_HASH_H
#define AMQP_HASH_H

#include "amqp.h"

#define AMQP_CHANNEL_TABLE_SIZE 256

typedef struct amqp_pool_link_t_ {
    amqp_pool_t pool;
    struct amqp_pool_link_t_ *next;
} amqp_pool_link_t;

typedef struct amqp_channel_table_entry_t_ {
    amqp_pool_link_t *pool;
    struct amqp_channel_table_entry_t_ *next;
    amqp_channel_t channel;
} amqp_channel_table_entry_t;

typedef struct amqp_channel_table_t_ {
    amqp_channel_table_entry_t *entries[AMQP_CHANNEL_TABLE_SIZE];
    amqp_pool_t entry_pool;
} amqp_channel_table_t;

typedef struct amqp_channel_pools_t_ {
  amqp_pool_link_t *current_pool;
  amqp_pool_link_t *free_pools;
  size_t pagesize;
  amqp_channel_table_t channel_pools;
} amqp_channel_pools_t;

void amqp_channel_pools_init(amqp_channel_pools_t *channel_pools, size_t pagesize);
void amqp_channel_pools_destroy(amqp_channel_pools_t *channel_pools);
amqp_pool_t *amqp_channel_pools_get(amqp_channel_pools_t *channel_pools);
int amqp_channel_pools_mark_current(amqp_channel_pools_t *channel_pools, amqp_channel_t channel);
void amqp_channel_pools_recycle(amqp_channel_pools_t *channel_pools);
void amqp_channel_pools_recycle_channel(amqp_channel_pools_t *channel_pools, amqp_channel_t channel);
void amqp_channel_pools_change_pagesize(amqp_channel_pools_t *channel_pools, size_t pagesize);


#endif /* AMQP_HASH_H */
