#ifndef librabbitmq_amqp_private_h
#define librabbitmq_amqp_private_h

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

#include "amqp.h"
#include "config.h"

/* Error numbering: Because of differences in error numbering on
 * different platforms, we want to keep error numbers opaque for
 * client code.  Internally, we encode the category of an error
 * (i.e. where its number comes from) in the top bits of the number
 * (assuming that an int has at least 32 bits).
 */
#define ERROR_CATEGORY_MASK (1 << 29)

#define ERROR_CATEGORY_CLIENT (0 << 29) /* librabbitmq error codes */
#define ERROR_CATEGORY_OS (1 << 29) /* OS-specific error codes */

/* librabbitmq error codes */
#define ERROR_NO_MEMORY 1
#define ERROR_BAD_AMQP_DATA 2
#define ERROR_UNKNOWN_CLASS 3
#define ERROR_UNKNOWN_METHOD 4
#define ERROR_GETHOSTBYNAME_FAILED 5
#define ERROR_INCOMPATIBLE_AMQP_VERSION 6
#define ERROR_CONNECTION_CLOSED 7
#define ERROR_BAD_AMQP_URL 8
#define ERROR_MAX 8

#include "socket.h"

#include <stdlib.h>

#define AMQP_MALLOC(type) (type*)malloc(sizeof(type))
#define AMQP_MALLOC_ARRAY(type, size) (type*)malloc(sizeof(type) * size)

#define AMQP_SAFE_POOL_ALLOC(pool, type) (type*)amqp_pool_alloc(pool, sizeof(type))

#ifdef __cplusplus
extern "C" {
#endif

extern char *amqp_os_error_string(int err);


/*
 * Connection states: XXX FIX THIS
 *
 * - CONNECTION_STATE_INITIAL: The initial state, when we cannot be
 *   sure if the next thing we will get is the first AMQP frame, or a
 *   protocol header from the server.
 *
 * - CONNECTION_STATE_IDLE: The normal state between
 *   frames. Connections may only be reconfigured, and the
 *   connection's pools recycled, when in this state. Whenever we're
 *   in this state, the inbound_buffer's bytes pointer must be NULL;
 *   any other state, and it must point to a block of memory allocated
 *   from the frame_pool.
 *
 * - CONNECTION_STATE_HEADER: Some bytes of an incoming frame have
 *   been seen, but not a complete frame header's worth.
 *
 * - CONNECTION_STATE_BODY: A complete frame header has been seen, but
 *   the frame is not yet complete. When it is completed, it will be
 *   returned, and the connection will return to IDLE state.
 *
 */
typedef enum amqp_connection_state_enum_ {
  CONNECTION_STATE_IDLE = 0,
  CONNECTION_STATE_INITIAL,
  CONNECTION_STATE_HEADER,
  CONNECTION_STATE_BODY
} amqp_connection_state_enum;

/* 7 bytes up front, then payload, then 1 byte footer */
#define HEADER_SIZE 7
#define FOOTER_SIZE 1

#define AMQP_PSEUDOFRAME_PROTOCOL_HEADER 'A'

#define INITIAL_FRAME_POOL_PAGE_SIZE 65536
#define INITIAL_INBOUND_SOCK_BUFFER_SIZE 131072

typedef struct amqp_link_t_ {
  struct amqp_link_t_ *next;
  void *data;
} amqp_link_t;

#define AMQP_HASHTABLE_SIZE 256

typedef struct amqp_pool_link_t_ {
    amqp_pool_t pool;
    struct amqp_pool_link_t_* next;
} amqp_pool_link_t;

typedef struct amqp_hashtable_entry_t_ {
    struct amqp_hashtable_entry_t_ *next;
    amqp_pool_link_t data;
    amqp_channel_t key;
} amqp_hashtable_entry_t;


typedef struct amqp_hashtable_t_ {
    amqp_hashtable_entry_t* entries[AMQP_HASHTABLE_SIZE];
} amqp_hashtable_t;

/**
  * Memory allocation scheme for amqp_connection_state_t:
  *
  * Most memory allocation in the library is done using an amqp_pool_t
  * Since pools can only be recycled when everything in them is no
  * longer being used we separate out usage so that it has an affinity
  * to the channel.
  *
  * When receiving data on a socket, the library allocates a buffer
  * to hold the data. This data comes from a 'frame_pool'. Once a complete
  * frame is received it is decoded.  If the frame contains a METHOD or
  * HEADER frame it is decoded, memory is allocated from a 'decoding_pool'.
  * Since there is little processing done for a BODY frame, its data is NOT
  * copied to a 'decoding_pool' allocated memory block, instead the frame_pool
  * that is responsible for the memory allocated to it is associated with a
  * channel, the pool is returned when the memory for a channel is recycled.
  *
  * Relevant amqp_connection_state_t elements:
  * - pool_cache_pool - this is where frame_pools are allocated from. We assume
  *                     frame pools will last the lifetime of the connection object
  * - frame_pool - this points to the 'current' frame_pool. This is NULL if there isn't
  *                a 'current' frame pool.  To get the current frame pool one
  *                should use the amqp_get_frame_pool()
  * - frame_pool_cache - this is a stack of the frame_pools that are not in use.
  *                      a frame_pool will end up here (in a recycled state) on
  *                      when a channel's pools are recycled
  * - decoding_pools - this is a chained hashtable of the decoding_pools. They are
  *                    created as needed by using the amqp_get_decoding_pool().
  *                    Each amqp_pool_t also contains a next pointer which frame_pools
  *                    maybe chained to the decoding pool. The frame_pools are
  *                    returned to the frame_pool_cache when associated decoding_pool
  *                    is recycled or destroyed using one of:
  *                    amqp_recycle_decoding_pool
  *                    amqp_recycle_all_decoding_pools
  *                    amqp_destroy_all_decoding_pools
  */

struct amqp_connection_state_t_ {
  amqp_pool_t pool_cache_pool;

  amqp_pool_link_t *frame_pool;
  amqp_pool_link_t *frame_pool_cache;

  amqp_hashtable_t decoding_pools;

  amqp_connection_state_enum state;

  int channel_max;
  int frame_max;
  int heartbeat;
  amqp_bytes_t inbound_buffer;

  size_t inbound_offset;
  size_t target_size;

  amqp_bytes_t outbound_buffer;

  int sockfd;
  amqp_bytes_t sock_inbound_buffer;
  size_t sock_inbound_offset;
  size_t sock_inbound_limit;

  amqp_link_t *first_queued_frame;
  amqp_link_t *last_queued_frame;

  amqp_rpc_reply_t most_recent_api_result;
};

static inline void *amqp_offset(void *data, size_t offset)
{
  return (char *)data + offset;
}

/* This macro defines the encoding and decoding functions associated with a
   simple type. */

#define DECLARE_CODEC_BASE_TYPE(bits, htonx, ntohx)                         \
                                                                            \
static inline void amqp_e##bits(void *data, size_t offset,                  \
                                uint##bits##_t val)                         \
{									    \
  /* The AMQP data might be unaligned. So we encode and then copy the       \
     result into place. */		   				    \
  uint##bits##_t res = htonx(val);	   				    \
  memcpy(amqp_offset(data, offset), &res, bits/8);                          \
}                                                                           \
                                                                            \
static inline uint##bits##_t amqp_d##bits(void *data, size_t offset)        \
{			      		   				    \
  /* The AMQP data might be unaligned.  So we copy the source value	    \
     into a variable and then decode it. */				    \
  uint##bits##_t val;	      						    \
  memcpy(&val, amqp_offset(data, offset), bits/8);                          \
  return ntohx(val);							    \
}                                                                           \
                                                                            \
static inline int amqp_encode_##bits(amqp_bytes_t encoded, size_t *offset,  \
                                     uint##bits##_t input)                  \
                                                                            \
{                                                                           \
  size_t o = *offset;                                                       \
  if ((*offset = o + bits / 8) <= encoded.len) {                            \
    amqp_e##bits(encoded.bytes, o, input);                                  \
    return 1;                                                               \
  }                                                                         \
  else {                                                                    \
    return 0;                                                               \
  }                                                                         \
}                                                                           \
                                                                            \
static inline int amqp_decode_##bits(amqp_bytes_t encoded, size_t *offset,  \
                                     uint##bits##_t *output)                \
                                                                            \
{                                                                           \
  size_t o = *offset;                                                       \
  if ((*offset = o + bits / 8) <= encoded.len) {                            \
    *output = amqp_d##bits(encoded.bytes, o);                               \
    return 1;                                                               \
  }                                                                         \
  else {                                                                    \
    return 0;                                                               \
  }                                                                         \
}

#ifndef WORDS_BIGENDIAN

#define DECLARE_XTOXLL(func)                      \
static inline uint64_t func##ll(uint64_t val)     \
{                                                 \
  union {                                         \
    uint64_t whole;                               \
    uint32_t halves[2];                           \
  } u;                                            \
  uint32_t t;                                     \
  u.whole = val;                                  \
  t = u.halves[0];                                \
  u.halves[0] = func##l(u.halves[1]);             \
  u.halves[1] = func##l(t);                       \
  return u.whole;                                 \
}

#else

#define DECLARE_XTOXLL(func)                      \
static inline uint64_t func##ll(uint64_t val)     \
{                                                 \
  union {                                         \
    uint64_t whole;                               \
    uint32_t halves[2];                           \
  } u;                                            \
  u.whole = val;                                  \
  u.halves[0] = func##l(u.halves[0]);             \
  u.halves[1] = func##l(u.halves[1]);             \
  return u.whole;                                 \
}

#endif

DECLARE_XTOXLL(hton)
DECLARE_XTOXLL(ntoh)

DECLARE_CODEC_BASE_TYPE(8, (uint8_t), (uint8_t))
DECLARE_CODEC_BASE_TYPE(16, htons, ntohs)
DECLARE_CODEC_BASE_TYPE(32, htonl, ntohl)
DECLARE_CODEC_BASE_TYPE(64, htonll, ntohll)

static inline int amqp_encode_bytes(amqp_bytes_t encoded, size_t *offset,
				    amqp_bytes_t input)
{
  size_t o = *offset;
  if ((*offset = o + input.len) <= encoded.len) {
    memcpy(amqp_offset(encoded.bytes, o), input.bytes, input.len);
    return 1;
  }
  else {
    return 0;
  }
}

static inline int amqp_decode_bytes(amqp_bytes_t encoded, size_t *offset,
				    amqp_bytes_t *output, size_t len)
{
  size_t o = *offset;
  if ((*offset = o + len) <= encoded.len) {
    output->bytes = amqp_offset(encoded.bytes, o);
    output->len = len;
    return 1;
  }
  else {
    return 0;
  }
}

extern void amqp_abort(const char *fmt, ...);

/**
  * Hashing function for the decoding pools table.
  *
  * We assume people will use channels in monotoic increasing order
  * thus just doing key % table size will lead to perfect utilization
  */
static int amqp_hashtable_channel_hash(amqp_channel_t channel);

/**
  * Initializes the hashtable
  *
  * Just sets everything to zero so we don't deal with junk values
  */
static void amqp_hashtable_init(amqp_hashtable_t* table);

/**
  * Adds a pool to the hashtable with the given channel as a key.
  *
  * NOTE: this is an internal function, you should probably use amqp_get_decoding_pool instead
  *
  * Will return a pointer to the new amqp_pool_t on success.
  *  Memory is owned by the hashtable and will be freed when amqp_destroy_all_decoding_pools is called
  * Will return NULL if:
  *  - the amqp_channel_t already exists in the hashtable
  *  - memory allocation fails
  */
static amqp_pool_link_t* amqp_hashtable_add_pool(amqp_hashtable_t* table, amqp_channel_t channel);

/**
  * Gets a pool from the hashtable with the given channel as a key
  *
  * NOTE: this is an internal function, you should probably use amqp_get_decoding_pool instead
  *
  * Will return a pointer to the amqp_pool_t on success.
  *  Memory is owned by the hashtable and will be freed when amqp_destroy_all_decoding_pools is called
  * Will return NULL if:
  *  - the key does not exist in the hash table
  */
static amqp_pool_link_t* amqp_hashtable_get_pool(amqp_hashtable_t* table, amqp_channel_t channel);

static void amqp_recycle_decoding_pool_inner(amqp_connection_state_t state, amqp_pool_link_t* decoding_pool);

/**
  * Gets the decoding pool for a specified channel, allocating if it doesn't exist
  *
  * Will return a pointer to the amqp_pool_t associated with the channe on success
  *  Memory is owned by the state object and will be freed with amqp_destroy_all_decoding_pools is called
  * The state object owns the decoding pool.
  * The pool should be recycled using the amqp_recycle_channel_pool or amqp_recycle_all_channel_pools function
  * All pools associated with the decoding pool structure can be destroyed with amqp_destroy_all_channel_pools
  */
static amqp_pool_link_t* amqp_get_decoding_pool_link(amqp_connection_state_t state, amqp_channel_t channel);

/**
  * Gets the decoding pool for a specified channel, allocating if it doesn't exist
  *
  * Will return a pointer to the amqp_pool_t associated with the channe on success
  *  Memory is owned by the state object and will be freed with amqp_destroy_all_decoding_pools is called
  * The state object owns the decoding pool.
  * The pool should be recycled using the amqp_recycle_channel_pool or amqp_recycle_all_channel_pools function
  * All pools associated with the decoding pool structure can be destroyed with amqp_destroy_all_channel_pools
  */
static amqp_pool_link_t* amqp_get_decoding_pool_link(amqp_connection_state_t state, amqp_channel_t channel);

extern amqp_pool_t* amqp_get_decoding_pool(amqp_connection_state_t state, amqp_channel_t channel);

/**
  * Recycles the decoding amqp_pool_t associated the channel, and recycles and returns any frame pools to the frame_pool_cache
  *
  * Recycles the pool by calling recycle_amqp_pool
  */
extern void amqp_recycle_decoding_pool(amqp_connection_state_t state, amqp_channel_t channel);

/**
  * Recycles all of the decoding amqp_pool_t s, and recycles and returns any frame pools to the frame_pool_cache
  *
  * Recycles the pools by calling recycle_amqp_pool
  */
extern void amqp_recycle_all_decoding_pools(amqp_connection_state_t state);

/**
  * Destroys all of the decoding amqp_pool_t's, and returns any frame pools to the frame_pool_cache WITHOUT destroying them
  *
  * Emptys theh pools by calling empty_amqp_pool
  */
static void amqp_destroy_all_decoding_pools(amqp_connection_state_t state);

/* Gets the current frame pool.
 *
 * If there's already a 'current' frame pool it returns that
 *	If there isn't a current frame pool, it attempts to get one from the cache
 *  If the cache is empty it creates a new one
 * Will return NULL on a out of memory condition
 */
static amqp_pool_link_t* amqp_get_frame_pool_link(amqp_connection_state_t state);

extern amqp_pool_t* amqp_get_frame_pool(amqp_connection_state_t state);

/**
  * Moves gets the current frame_pool and associates it with the decoding pool for the specified channel
  *
  * This function creates pools if they do not already exist
  * Returns 0 on success, non-zero on error, -ERROR_NO_MEMORY on allocation failure
  */
extern int amqp_move_frame_pool(amqp_connection_state_t state, amqp_channel_t channel);


/**
  * Destroys all of the frame pools, both current, and cached, then emptys the pool_frame_pool
  */
static void amqp_destroy_all_frame_pools(amqp_connection_state_t state);

/**
  * Initializes both the frame and decoding pool structures
  */
extern void amqp_init_all_pools(amqp_connection_state_t state);


/**
  * Destroys the frame and decoding pool structures
  */
extern void amqp_destroy_all_pools(amqp_connection_state_t state);

#ifdef __cplusplus
}
#endif
#endif
