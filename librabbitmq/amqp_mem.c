/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * The Original Code is librabbitmq.
 *
 * The Initial Developer of the Original Code is VMware, Inc.
 * Portions created by VMware are Copyright (c) 2007-2011 VMware, Inc.
 *
 * Portions created by Tony Garnock-Jones are Copyright (c) 2009-2010
 * VMware, Inc. and Tony Garnock-Jones.
 *
 * All rights reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License Version 2 or later (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of those
 * above. If you wish to allow use of your version of this file only
 * under the terms of the GPL, and not to allow others to use your
 * version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the
 * notice and other provisions required by the GPL. If you do not
 * delete the provisions above, a recipient may use your version of
 * this file under the terms of any one of the MPL or the GPL.
 *
 * ***** END LICENSE BLOCK *****
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <assert.h>

#include "amqp.h"
#include "amqp_private.h"
#include "config.h"

#define INITIAL_DECODING_POOL_PAGE_SIZE 131072

char const *amqp_version(void) {
  return VERSION; /* defined in config.h */
}

void init_amqp_pool(amqp_pool_t *pool, size_t pagesize) {
  pool->pagesize = pagesize ? pagesize : 4096;

  pool->pages.num_blocks = 0;
  pool->pages.blocklist = NULL;

  pool->large_blocks.num_blocks = 0;
  pool->large_blocks.blocklist = NULL;

  pool->next_page = 0;
  pool->alloc_block = NULL;
  pool->alloc_used = 0;
}

static void empty_blocklist(amqp_pool_blocklist_t *x) {
  int i;

  for (i = 0; i < x->num_blocks; i++) {
    free(x->blocklist[i]);
  }
  if (x->blocklist != NULL) {
    free(x->blocklist);
  }
  x->num_blocks = 0;
  x->blocklist = NULL;
}

void recycle_amqp_pool(amqp_pool_t *pool) {
  empty_blocklist(&pool->large_blocks);
  pool->next_page = 0;
  pool->alloc_block = NULL;
  pool->alloc_used = 0;
}

void empty_amqp_pool(amqp_pool_t *pool) {
  recycle_amqp_pool(pool);
  empty_blocklist(&pool->pages);
}

/* Returns 1 on success, 0 on failure */
static int record_pool_block(amqp_pool_blocklist_t *x, void *block) {
  size_t blocklistlength = sizeof(void *) * (x->num_blocks + 1);

  if (x->blocklist == NULL) {
    x->blocklist = (void**)malloc(blocklistlength);
    if (x->blocklist == NULL)
      return 0;
  } else {
    void *newbl = realloc(x->blocklist, blocklistlength);
    if (newbl == NULL)
      return 0;
    x->blocklist = (void**)newbl;
  }

  x->blocklist[x->num_blocks] = block;
  x->num_blocks++;
  return 1;
}

void *amqp_pool_alloc(amqp_pool_t *pool, size_t amount) {
  if (amount == 0) {
    return NULL;
  }

  amount = (amount + 7) & (~7); /* round up to nearest 8-byte boundary */

  if (amount > pool->pagesize) {
    void *result = calloc(1, amount);
    if (result == NULL) {
      return NULL;
    }
    if (!record_pool_block(&pool->large_blocks, result))
      return NULL;
    return result;
  }

  if (pool->alloc_block != NULL) {
    assert(pool->alloc_used <= pool->pagesize);

    if (pool->alloc_used + amount <= pool->pagesize) {
      void *result = pool->alloc_block + pool->alloc_used;
      pool->alloc_used += amount;
      return result;
    }
  }

  if (pool->next_page >= pool->pages.num_blocks) {
    pool->alloc_block = (char*)calloc(1, pool->pagesize);
    if (pool->alloc_block == NULL) {
      return NULL;
    }
    if (!record_pool_block(&pool->pages, pool->alloc_block))
      return NULL;
    pool->next_page = pool->pages.num_blocks;
  } else {
    pool->alloc_block = (char*)pool->pages.blocklist[pool->next_page];
    pool->next_page++;
  }

  pool->alloc_used = amount;

  return pool->alloc_block;
}

void amqp_pool_alloc_bytes(amqp_pool_t *pool, size_t amount, amqp_bytes_t *output) {
  output->len = amount;
  output->bytes = amqp_pool_alloc(pool, amount);
}

amqp_bytes_t amqp_cstring_bytes(char const *cstr) {
  amqp_bytes_t result;
  result.len = strlen(cstr);
  result.bytes = (void *) cstr;
  return result;
}

amqp_bytes_t amqp_bytes_malloc_dup(amqp_bytes_t src) {
  amqp_bytes_t result;
  result.len = src.len;
  result.bytes = malloc(src.len);
  if (result.bytes != NULL) {
    memcpy(result.bytes, src.bytes, src.len);
  }
  return result;
}

amqp_bytes_t amqp_bytes_malloc(size_t amount) {
  amqp_bytes_t result;
  result.len = amount;
  result.bytes = malloc(amount); /* will return NULL if it fails */
  return result;
}

void amqp_bytes_free(amqp_bytes_t bytes) {
  free(bytes.bytes);
}

static amqp_pool_link_t* amqp_get_frame_pool_link(amqp_connection_state_t state) {
  int page_size = (0 == state->frame_max ? INITIAL_FRAME_POOL_PAGE_SIZE 
                                         : state->frame_max);

  assert(NULL != state);

  if (NULL != state->frame_pool)
    return state->frame_pool;

  if (NULL != state->frame_pool_cache) {
    state->frame_pool = state->frame_pool_cache;
    state->frame_pool_cache = state->frame_pool->next;
    state->frame_pool->next = NULL;

    if (state->frame_pool->pool.pagesize != page_size) {
      empty_amqp_pool(&state->frame_pool->pool);
      init_amqp_pool(&state->frame_pool->pool, page_size);
    }
    return state->frame_pool;
  }

  state->frame_pool = AMQP_SAFE_POOL_ALLOC(&state->pool_cache_pool, 
                                           amqp_pool_link_t);

  if (NULL == state->frame_pool)
    return NULL;

  init_amqp_pool(&state->frame_pool->pool, page_size);

  return state->frame_pool;
}

extern amqp_pool_t* amqp_get_frame_pool(amqp_connection_state_t state) {

  amqp_pool_link_t* pool = amqp_get_frame_pool_link(state);

  if (NULL == pool) {
    return NULL;
  } else {
    return &pool->pool;
  }
}

extern int amqp_move_frame_pool(amqp_connection_state_t state, amqp_channel_t channel) {

  amqp_pool_link_t* decoding_pool = NULL;
  amqp_pool_link_t* frame_pool = NULL;

  decoding_pool = amqp_get_decoding_pool_link(state, channel);
  if (NULL == decoding_pool) {
    return -ERROR_NO_MEMORY;
  }

  frame_pool = amqp_get_frame_pool_link(state);
  if (NULL == frame_pool) {
    return -ERROR_NO_MEMORY;
  }

  state->frame_pool = NULL;

  frame_pool->next = decoding_pool->next;
  decoding_pool->next = frame_pool;

  return 0;
}

static void amqp_destroy_all_frame_pools(amqp_connection_state_t state) {
  amqp_pool_link_t* frame_pool = NULL;

  assert(NULL != state);

  if (NULL != state->frame_pool) {
    empty_amqp_pool(&state->frame_pool->pool);
  }

  for (frame_pool = state->frame_pool_cache;
       NULL != frame_pool;
       frame_pool = frame_pool->next) {
    empty_amqp_pool(&frame_pool->pool);
  }

  empty_amqp_pool(&state->pool_cache_pool);
}

static int amqp_hashtable_channel_hash(amqp_channel_t channel) {
  return channel % AMQP_HASHTABLE_SIZE;
}

static void amqp_hashtable_init(amqp_hashtable_t* table) {
  assert(NULL != table);

  memset(table, 0, sizeof(amqp_hashtable_t));
}

static amqp_pool_link_t* amqp_hashtable_get_pool(amqp_hashtable_t* table, 
                                                 amqp_channel_t channel) {
  amqp_hashtable_entry_t* entry = NULL;
  int index = 0;

  assert(NULL != table);

  index = amqp_hashtable_channel_hash(channel);

  assert(AMQP_HASHTABLE_SIZE > index);
  entry = table->entries[index];

  while (NULL != entry) {
    if (channel == entry->key) {
      return &entry->data;
    }

    entry = entry->next;
  }

  return NULL;
}

static amqp_pool_link_t* amqp_hashtable_add_pool(amqp_hashtable_t* table, 
                                                 amqp_channel_t channel) {
amqp_hashtable_entry_t* new_entry = NULL;
  int index = 0;

  assert(NULL != table);

  if (NULL != amqp_hashtable_get_pool(table, channel)) {
    return NULL;
  }

  index = amqp_hashtable_channel_hash(channel);
  assert(AMQP_HASHTABLE_SIZE > index);

  new_entry = AMQP_MALLOC(amqp_hashtable_entry_t);
  if (NULL == new_entry) {
    return NULL;
  }
  memset(new_entry, 0, sizeof(amqp_hashtable_entry_t));
  new_entry->key = channel;
  new_entry->next = table->entries[index];
  table->entries[index] = new_entry;

  return &new_entry->data;
}

static amqp_pool_link_t* amqp_get_decoding_pool_link(amqp_connection_state_t state,
                                                     amqp_channel_t channel) {
  amqp_pool_link_t* pool = NULL;

  assert(NULL != state);

  // Try to get the decoding pool
  pool = amqp_hashtable_get_pool(&state->decoding_pools, channel);

  if (NULL == pool) {
    pool = amqp_hashtable_add_pool(&state->decoding_pools, channel);
    init_amqp_pool(&pool->pool, INITIAL_DECODING_POOL_PAGE_SIZE);
  }
  return pool;
}

amqp_pool_t* amqp_get_decoding_pool(amqp_connection_state_t state, amqp_channel_t channel)
{
  amqp_pool_link_t* pool = amqp_get_decoding_pool_link(state, channel);

  if (NULL == pool) {
    return NULL;
  } else {
    return &pool->pool;
  }
}

static void amqp_recycle_decoding_pool_inner(amqp_connection_state_t state,
                                             amqp_pool_link_t* decoding_pool) {
  assert(NULL != state);
  assert(NULL != decoding_pool);

  recycle_amqp_pool(&decoding_pool->pool);


  if (NULL != decoding_pool->next) {
    amqp_pool_link_t* frame_pool = decoding_pool->next;
    amqp_pool_link_t* last_frame_pool = NULL;

    for (frame_pool = decoding_pool->next;
         NULL != frame_pool;
         last_frame_pool = frame_pool, frame_pool = frame_pool->next) {

      recycle_amqp_pool(&frame_pool->pool);
    }

    last_frame_pool->next = state->frame_pool_cache;
    state->frame_pool_cache = decoding_pool->next;
    decoding_pool->next = NULL;
  }
}

extern void amqp_recycle_decoding_pool(amqp_connection_state_t state, 
                                       amqp_channel_t channel) {
  amqp_pool_link_t* decoding_pool = NULL;

  assert(NULL != state);

  decoding_pool = amqp_get_decoding_pool_link(state, channel);

  if (NULL == decoding_pool) {
    return;
  }

  amqp_recycle_decoding_pool_inner(state, decoding_pool);
}

extern void amqp_recycle_all_decoding_pools(amqp_connection_state_t state)
{
  int i = 0;
  assert(NULL != state);

  for (i = 0; i < AMQP_HASHTABLE_SIZE; ++i) {
    if (NULL != state->decoding_pools.entries[i]) {
      amqp_hashtable_entry_t* entry = state->decoding_pools.entries[i];
      for ( ; entry != NULL; entry = entry->next) {
        amqp_recycle_decoding_pool_inner(state, &entry->data);
      }
    }
  }
}

static void amqp_destroy_all_decoding_pools(amqp_connection_state_t state) {
  int i = 0;
  assert(NULL != state);
  
  for (i = 0; i < AMQP_HASHTABLE_SIZE; ++i) {
    if (NULL != state->decoding_pools.entries[i]) {
      amqp_hashtable_entry_t* entry = state->decoding_pools.entries[i];
      while (entry != NULL) {
        amqp_hashtable_entry_t* last_entry = NULL;
        amqp_pool_link_t* decoding_pool = &entry->data;
        amqp_pool_link_t* last_frame_pool = NULL;

        empty_amqp_pool(&decoding_pool->pool);

        if (NULL != decoding_pool->next) {
          last_frame_pool = decoding_pool->next;

          for (last_frame_pool = decoding_pool->next;
            NULL != last_frame_pool->next;
            last_frame_pool = last_frame_pool->next) { /* empty */ }

          last_frame_pool->next = state->frame_pool_cache;
          state->frame_pool_cache = decoding_pool->next;
          decoding_pool->next = NULL;
        }

        last_entry = entry->next;
        free(entry);
        entry = last_entry;
      }
    }
  }
}

extern void amqp_init_all_pools(amqp_connection_state_t state) {
  state->frame_pool = NULL;
  state->frame_pool_cache = NULL;
  state->frame_max = 0;
  init_amqp_pool(&state->pool_cache_pool, sizeof(amqp_pool_t)*16);
  amqp_hashtable_init(&state->decoding_pools);
}

extern void amqp_destroy_all_pools(amqp_connection_state_t state) {
  amqp_destroy_all_decoding_pools(state);
  amqp_destroy_all_frame_pools(state);
}
