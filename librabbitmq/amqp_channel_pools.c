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

#include "amqp_channel_pools.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void amqp_channel_table_init(amqp_channel_table_t *table);
static void amqp_channel_table_destroy(amqp_channel_table_t *table);

static int amqp_channel_table_add_pool(amqp_channel_table_t *table, amqp_channel_t channel, amqp_pool_link_t *pool);

static amqp_pool_link_t *amqp_channel_table_get_used_pools(amqp_channel_table_t *table, amqp_channel_t channel);
static amqp_pool_link_t *amqp_channel_table_get_all_used_pools(amqp_channel_table_t *table);


static amqp_channel_table_entry_t *amqp_channel_table_get_entry(amqp_channel_table_t *table, amqp_channel_t key);
static amqp_channel_table_entry_t *amqp_channel_entry_create(amqp_pool_t *from_pool);


static amqp_pool_link_t *amqp_channel_pool_create(size_t pagesize)
{
  amqp_pool_link_t *pool = malloc(sizeof(amqp_pool_link_t));

  if (NULL == pool)
  {
    return NULL;
  }

  init_amqp_pool(&pool->pool, pagesize);
  pool->next = NULL;

  return pool;
}

void amqp_channel_pool_free(amqp_pool_link_t *pool)
{
  assert(pool != NULL);

  empty_amqp_pool(&pool->pool);
  free(pool);
}

void amqp_channel_pools_init(amqp_channel_pools_t *channel_pools, size_t pagesize)
{
  assert(NULL != channel_pools);
  assert(0 < pagesize);

  channel_pools->current_pool = NULL;
  channel_pools->free_pools = NULL;
  channel_pools->pagesize = pagesize;
  amqp_channel_table_init(&channel_pools->channel_pools);
}

void amqp_channel_pools_destroy(amqp_channel_pools_t *channel_pools)
{
  amqp_pool_link_t *current;

  assert(NULL != channel_pools);

  if (NULL != channel_pools->current_pool)
  {
    amqp_channel_pool_free(channel_pools->current_pool);
    channel_pools->current_pool = NULL;
  }

  current = channel_pools->free_pools;
  while (NULL != current)
  {
    amqp_pool_link_t *next;
    next = current->next;
    amqp_channel_pool_free(current);
    current = next;
  }

  amqp_channel_table_destroy(&channel_pools->channel_pools);
}

amqp_pool_t *amqp_channel_pools_get(amqp_channel_pools_t *channel_pools)
{
  assert(NULL != channel_pools);

  if (NULL != channel_pools->current_pool)
  {
    return &channel_pools->current_pool->pool;
  }
  else if (NULL != channel_pools->free_pools)
  {
    channel_pools->current_pool = channel_pools->free_pools;
    channel_pools->free_pools = channel_pools->current_pool->next;
    channel_pools->current_pool->next = NULL;

    return &channel_pools->current_pool->pool;
  }
  else
  {
    channel_pools->current_pool = amqp_channel_pool_create(channel_pools->pagesize);
    if (NULL == channel_pools->current_pool)
    {
      return NULL;
    }
    return &channel_pools->current_pool->pool;
  }
}

void amqp_channel_pools_change_pagesize(amqp_channel_pools_t *channel_pools, size_t pagesize)
{
  amqp_pool_link_t *current;

  assert(NULL != channel_pools);
  assert(0 < pagesize);

  channel_pools->pagesize = pagesize;

  current = channel_pools->free_pools;

  for (current = channel_pools->free_pools;
       NULL != current;
       current = current->next)
  {
    if (channel_pools->pagesize != current->pool.pagesize)
    {
      empty_amqp_pool(&current->pool);
      init_amqp_pool(&current->pool, channel_pools->pagesize);
    }
  }
}

int amqp_channel_pools_mark_current(amqp_channel_pools_t *channel_pools, amqp_channel_t channel)
{
  int ret;
  assert(NULL != channel_pools);

  if (NULL == channel_pools->current_pool)
  {
    return 0;
  }

  ret = amqp_channel_table_add_pool(&channel_pools->channel_pools,
                                    channel, channel_pools->current_pool);
  channel_pools->current_pool = NULL;

  return ret;
}

static void amqp_channel_pool_recycle_inner(amqp_channel_pools_t *channel_pools,
                                            amqp_pool_link_t *current)
{
  assert(channel_pools != NULL);

  while (current != NULL)
  {
    amqp_pool_link_t *next;
    if (channel_pools->pagesize != current->pool.pagesize)
    {
      empty_amqp_pool(&current->pool);
      init_amqp_pool(&current->pool, channel_pools->pagesize);
    }
    else
    {
      recycle_amqp_pool(&current->pool);
    }

    next = current->next;
    current->next = channel_pools->free_pools;
    channel_pools->free_pools = current;

    current = next;
  }
}

void amqp_channel_pools_recycle(amqp_channel_pools_t *channel_pools)
{
  amqp_pool_link_t *current;
  assert(NULL != channel_pools);

  current = amqp_channel_table_get_all_used_pools(&channel_pools->channel_pools);
  amqp_channel_pool_recycle_inner(channel_pools, current);
}

void amqp_channel_pools_recycle_channel(amqp_channel_pools_t *channel_pools, amqp_channel_t channel)
{
  amqp_pool_link_t *current;
  assert(NULL != channel_pools);

  current = amqp_channel_table_get_used_pools(&channel_pools->channel_pools, channel);
  amqp_channel_pool_recycle_inner(channel_pools, current);
}

void amqp_channel_table_init(amqp_channel_table_t *table)
{
  assert(table != NULL);
  
  memset(&table->entries, 0,
         sizeof(amqp_channel_table_entry_t*) * AMQP_CHANNEL_TABLE_SIZE);
  init_amqp_pool(&table->entry_pool, sizeof(amqp_channel_table_entry_t) * 16);
}

void amqp_channel_table_destroy(amqp_channel_table_t *table)
{
  int i;
  
  assert(table != NULL);
  
  for (i = 0; i < AMQP_CHANNEL_TABLE_SIZE; ++i)
  {
    amqp_channel_table_entry_t *entry;
    for (entry = table->entries[i];
         entry != NULL; entry = entry->next)
    {
      amqp_pool_link_t *pool = entry->pool;
      while (NULL != pool)
      {
        amqp_pool_link_t *next = pool->next;

        amqp_channel_pool_free(pool);

        pool = next;
      }
    }
  }
  
  empty_amqp_pool(&table->entry_pool);
}


int amqp_channel_table_add_pool(amqp_channel_table_t *table, amqp_channel_t channel, amqp_pool_link_t *pool)
{
  amqp_channel_table_entry_t *entry;
  
  assert(NULL != table);
  assert(NULL != pool);
  assert(NULL == pool->next);
  
  entry = amqp_channel_table_get_entry(table, channel);
  if (NULL == entry)
    return -1;
  
  pool->next = entry->pool;
  entry->pool = pool;

  return 0;
}

amqp_pool_link_t *amqp_channel_table_get_used_pools(amqp_channel_table_t *table, amqp_channel_t channel)
{
  amqp_channel_table_entry_t *entry;
  amqp_pool_link_t *pool;
  
  assert(NULL != table);
  
  entry = amqp_channel_table_get_entry(table, channel);
  
  pool = entry->pool;
  entry->pool = NULL;
  
  return pool;
}

amqp_pool_link_t *amqp_channel_table_get_all_used_pools(amqp_channel_table_t *table)
{
  amqp_pool_link_t *results;
  int i;
  
  assert(NULL != table);
  
  results = NULL;
  
  for (i = 0; i < AMQP_CHANNEL_TABLE_SIZE; ++i)
  {
    amqp_channel_table_entry_t *entry;
    for (entry = table->entries[i];
         entry != NULL; entry = entry->next)
    {
      if (NULL != entry->pool)
      {
        amqp_pool_link_t *last = entry->pool;
        
        while (NULL != last->next)
        {
          last = last->next;
        }

        last->next = results;
        results = entry->pool;
        entry->pool = NULL;
      }
    }
  }
  
  return results;
}

static int amqp_channel_table_hash(amqp_channel_t key)
{
  return key % AMQP_CHANNEL_TABLE_SIZE;
}

amqp_channel_table_entry_t *amqp_channel_table_get_entry(amqp_channel_table_t *table,
                                                         amqp_channel_t key)
{
  int entry_key;
  amqp_channel_table_entry_t *entry;
  
  assert(table != NULL);
  
  entry_key = amqp_channel_table_hash(key);
  
  for (entry = table->entries[entry_key];
       entry != NULL; entry = entry->next)
  {
    if (key == entry->channel)
    {
      return entry;
    }
  }
  
  /* Entry doesn't exist, create it */
  entry = amqp_channel_entry_create(&table->entry_pool);
  if (NULL == entry)
  {
    return NULL;
  }
  
  entry->channel = key;
  entry->next = table->entries[entry_key];
  table->entries[entry_key] = entry;
  
  return entry;
}

static amqp_channel_table_entry_t *amqp_channel_entry_create(amqp_pool_t *from_pool)
{
  amqp_channel_table_entry_t *entry;
  
  assert(NULL != from_pool);
  
  entry = amqp_pool_alloc(from_pool, sizeof(amqp_channel_table_entry_t));
  if (NULL == entry)
  {
    return NULL;
  }
  
  entry->next = NULL;
  entry->pool = NULL;
  
  return entry;
}
