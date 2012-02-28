#include "amqp_mem.c"

#include <gtest/gtest.h>

#include <numeric>


class test_pools : public ::testing::Test
{
public:
  test_pools()
  {
    amqp_init_all_pools(&state);
  }

  ~test_pools()
  {
    amqp_destroy_all_pools(&state);
  }

  struct amqp_connection_state_t_ state;
};


TEST_F(test_pools, initialization)
{
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool_cache);
}

TEST_F(test_pools, get_frame_pool)
{
  amqp_pool_link_t* frame_pool_link = amqp_get_frame_pool_link(&state);
  EXPECT_NE((amqp_pool_link_t*)NULL, frame_pool_link);
  EXPECT_EQ((amqp_pool_link_t*)NULL, frame_pool_link->next);

  EXPECT_EQ(frame_pool_link, state.frame_pool);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool_cache);


  amqp_pool_link_t* same_pool_link = amqp_get_frame_pool_link(&state);
  EXPECT_EQ(frame_pool_link, same_pool_link);
  EXPECT_EQ((amqp_pool_link_t*)NULL, same_pool_link->next);

  EXPECT_EQ(same_pool_link, state.frame_pool);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool_cache);
}

TEST_F(test_pools, move_frame_pool)
{
  amqp_channel_t channel = 38;

  amqp_pool_link_t* decoding_pool = amqp_get_decoding_pool_link(&state, channel);
  amqp_pool_link_t* frame_pool = amqp_get_frame_pool_link(&state);

  EXPECT_EQ(0, amqp_move_frame_pool(&state, channel));

  EXPECT_EQ(frame_pool, decoding_pool->next);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool_cache);
}

TEST_F(test_pools, move_frame_pool_reuse)
{
  amqp_channel_t channel = 42;
  amqp_pool_link_t* frame_pool = amqp_get_frame_pool_link(&state);

  EXPECT_EQ(0, amqp_move_frame_pool(&state, channel));

  amqp_recycle_decoding_pool(&state, channel);

  EXPECT_EQ(frame_pool, state.frame_pool_cache);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool);


  EXPECT_EQ(frame_pool, amqp_get_frame_pool_link(&state));
}

TEST_F(test_pools, test_frame_pool_reuse_one)
{
  amqp_channel_t channel1 = 174;
  amqp_channel_t channel2 = 231;
  amqp_pool_link_t* decoding_pool1 = amqp_get_decoding_pool_link(&state, channel1);
  amqp_pool_link_t* decoding_pool2 = amqp_get_decoding_pool_link(&state, channel2);
  EXPECT_NE(decoding_pool1, decoding_pool2);

  amqp_pool_link_t* frame_pool1 = amqp_get_frame_pool_link(&state);
  EXPECT_NE((amqp_pool_link_t*)NULL, frame_pool1);
  EXPECT_EQ(0, amqp_move_frame_pool(&state, channel1));

  amqp_pool_link_t* frame_pool2 = amqp_get_frame_pool_link(&state);
  EXPECT_NE((amqp_pool_link_t*)NULL, frame_pool2);
  EXPECT_EQ(0, amqp_move_frame_pool(&state, channel2));

  amqp_recycle_decoding_pool(&state, channel1);
  EXPECT_EQ((amqp_pool_link_t*)NULL, decoding_pool1->next);
  EXPECT_EQ(frame_pool1, state.frame_pool_cache);
  EXPECT_EQ(frame_pool1, amqp_get_frame_pool_link(&state));
  EXPECT_EQ(frame_pool1, state.frame_pool);

  EXPECT_EQ(frame_pool2, decoding_pool2->next);

  EXPECT_EQ(decoding_pool1, amqp_get_decoding_pool_link(&state, channel1));
  EXPECT_EQ(decoding_pool2, amqp_get_decoding_pool_link(&state, channel2));
}

TEST_F(test_pools, test_frame_pool_reuse_chain)
{
  amqp_channel_t channel = 6;
  amqp_pool_link_t* decoding_pool = amqp_get_decoding_pool_link(&state, channel);

  amqp_pool_link_t* frame_pool1 = amqp_get_frame_pool_link(&state);
  EXPECT_EQ(0, amqp_move_frame_pool(&state, channel));

  amqp_pool_link_t* frame_pool2 = amqp_get_frame_pool_link(&state);
  EXPECT_NE(frame_pool1, frame_pool2);
  EXPECT_EQ(0, amqp_move_frame_pool(&state, channel));

  amqp_recycle_decoding_pool(&state, channel);
  EXPECT_EQ((amqp_pool_link_t*)NULL, decoding_pool->next);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool);
  EXPECT_NE((amqp_pool_link_t*)NULL, state.frame_pool_cache);
  EXPECT_NE((amqp_pool_link_t*)NULL, state.frame_pool_cache->next);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool_cache->next->next);

  EXPECT_NE((amqp_pool_link_t*)NULL, amqp_get_frame_pool_link(&state));
  EXPECT_NE((amqp_pool_link_t*)NULL, state.frame_pool);
  EXPECT_NE((amqp_pool_link_t*)NULL, state.frame_pool_cache);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool_cache->next);
}


TEST_F(test_pools, test_frame_pool_reuse_all)
{
  amqp_channel_t channel1 = 25;
  amqp_channel_t channel2 = 91;
  amqp_pool_link_t* decoding_pool1 = amqp_get_decoding_pool_link(&state, channel1);
  amqp_pool_link_t* decoding_pool2 = amqp_get_decoding_pool_link(&state, channel2);
  EXPECT_NE(decoding_pool1, decoding_pool2);

  amqp_pool_link_t* frame_pool1 = amqp_get_frame_pool_link(&state);
  EXPECT_NE((amqp_pool_link_t*)NULL, frame_pool1);
  EXPECT_EQ(0, amqp_move_frame_pool(&state, channel1));

  amqp_pool_link_t* frame_pool2 = amqp_get_frame_pool_link(&state);
  EXPECT_NE((amqp_pool_link_t*)NULL, frame_pool2);
  EXPECT_EQ(0, amqp_move_frame_pool(&state, channel2));

  amqp_recycle_all_decoding_pools(&state);
  EXPECT_EQ((amqp_pool_link_t*)NULL, decoding_pool1->next);
  EXPECT_EQ((amqp_pool_link_t*)NULL, decoding_pool2->next);
  EXPECT_NE((amqp_pool_link_t*)NULL, state.frame_pool_cache);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool);

  amqp_pool_link_t* frame_pool_recycled = amqp_get_frame_pool_link(&state);
  EXPECT_TRUE(frame_pool_recycled == frame_pool1 || frame_pool_recycled == frame_pool2);
  EXPECT_EQ((amqp_pool_link_t*)NULL, frame_pool_recycled->next);
  EXPECT_EQ(frame_pool_recycled, state.frame_pool);
  EXPECT_NE((amqp_pool_link_t*)NULL, state.frame_pool_cache);
  EXPECT_EQ((amqp_pool_link_t*)NULL, state.frame_pool_cache->next);

  EXPECT_EQ(decoding_pool1, amqp_get_decoding_pool_link(&state, channel1));
  EXPECT_EQ(decoding_pool2, amqp_get_decoding_pool_link(&state, channel2));
}

TEST_F(test_pools, get_decoding_pool)
{
  amqp_channel_t channel = 32;
  amqp_pool_link_t* decoding_pool = amqp_get_decoding_pool_link(&state, channel);
  EXPECT_NE((amqp_pool_link_t*)NULL, decoding_pool);
  EXPECT_EQ((amqp_pool_link_t*)NULL, decoding_pool->next);

  amqp_pool_link_t* decoding_pool_same = amqp_get_decoding_pool_link(&state, channel);
  EXPECT_EQ(decoding_pool, decoding_pool_same);
  EXPECT_EQ((amqp_pool_link_t*)NULL, decoding_pool_same->next);
}

TEST_F(test_pools, get_decoding_pool_differs)
{
  amqp_channel_t channel = 0;
  amqp_pool_t* decoding_pool = amqp_get_decoding_pool(&state, channel);

  for (int i = 1; i <= std::numeric_limits<amqp_channel_t>::max(); ++i)
  {
    amqp_pool_t* other_pool = amqp_get_decoding_pool(&state, i);
    EXPECT_NE(decoding_pool, other_pool);
  }

  amqp_pool_t* decoding_pool_same = amqp_get_decoding_pool(&state, channel);
  EXPECT_EQ(decoding_pool, decoding_pool_same);
}


