#include "amqp_mem.c"
#include <gtest/gtest.h>

class test_hashtable : public ::testing::Test
{
public:
  test_hashtable()
  {
    amqp_hashtable_init(&state.decoding_pools);
  }

  ~test_hashtable()
  {
    amqp_destroy_all_decoding_pools(&state);
  }

  struct amqp_connection_state_t_ state;
};

TEST_F(test_hashtable, init)
{
  // The entire table should be zeroed
  for (int i = 0; i < AMQP_HASHTABLE_SIZE; ++i)
  {
    EXPECT_EQ(0, state.decoding_pools.entries[i]);
  }
}

TEST_F(test_hashtable, add_pool)
{
  amqp_pool_link_t* pool = amqp_hashtable_add_pool(&state.decoding_pools, 5);
  EXPECT_NE((amqp_pool_link_t*)NULL, pool);
}

TEST_F(test_hashtable, add_pool_duplicate)
{
  amqp_pool_link_t* pool = amqp_hashtable_add_pool(&state.decoding_pools, 0);
  EXPECT_NE((amqp_pool_link_t*)NULL, pool);

  pool = amqp_hashtable_add_pool(&state.decoding_pools, 0);
  EXPECT_EQ((amqp_pool_link_t*)NULL, pool);
}

TEST_F(test_hashtable, add_two_keys)
{
  const amqp_channel_t key1 = 2;
  const amqp_channel_t key2 = 3;

  EXPECT_NE(amqp_hashtable_channel_hash(key1), amqp_hashtable_channel_hash(key2));

  amqp_pool_link_t* pool1 = amqp_hashtable_add_pool(&state.decoding_pools, key1);
  amqp_pool_link_t* pool2 = amqp_hashtable_add_pool(&state.decoding_pools, key2);

  EXPECT_NE(pool1, pool2);

  amqp_pool_link_t* pool1a = amqp_hashtable_get_pool(&state.decoding_pools, key1);
  amqp_pool_link_t* pool2a = amqp_hashtable_get_pool(&state.decoding_pools, key2);

  EXPECT_EQ(pool1, pool1a);
  EXPECT_EQ(pool2, pool2a);
}

TEST_F(test_hashtable, add_key_collision)
{
  const amqp_channel_t key1 = 0;
  const amqp_channel_t key2 = key1 + AMQP_HASHTABLE_SIZE;

  ASSERT_EQ(amqp_hashtable_channel_hash(key1), amqp_hashtable_channel_hash(key2));

  amqp_pool_link_t* pool1 = amqp_hashtable_add_pool(&state.decoding_pools, key1);
  amqp_pool_link_t* pool2 = amqp_hashtable_add_pool(&state.decoding_pools, key2);

  EXPECT_NE(pool1, pool2);

  amqp_pool_link_t* pool1a = amqp_hashtable_get_pool(&state.decoding_pools, key1);
  amqp_pool_link_t* pool2a = amqp_hashtable_get_pool(&state.decoding_pools, key2);

  EXPECT_EQ(pool1, pool1a);
  EXPECT_EQ(pool2, pool2a);
}

TEST_F(test_hashtable, get_key_not_exist)
{
  amqp_pool_link_t* not_exist = amqp_hashtable_get_pool(&state.decoding_pools, 10);
  EXPECT_EQ((amqp_pool_link_t*)NULL, not_exist);
}

TEST_F(test_hashtable, get_key_not_exist_chained)
{
  amqp_channel_t key_exist = 24;
  amqp_channel_t key_not_exist = key_exist + AMQP_HASHTABLE_SIZE;

  ASSERT_EQ(amqp_hashtable_channel_hash(key_exist), amqp_hashtable_channel_hash(key_not_exist));

  amqp_hashtable_add_pool(&state.decoding_pools, key_exist);

  amqp_pool_link_t* pool = amqp_hashtable_get_pool(&state.decoding_pools, key_not_exist);
  EXPECT_EQ((amqp_pool_link_t*)NULL, pool);
}

