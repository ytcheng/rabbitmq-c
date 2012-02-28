project(testing C CXX)

include_directories(${LIBRABBITMQ_INCLUDE_DIRS})

include_directories(${CMAKE_SOURCE_DIR}/third-party/gtest-1.6.0)

add_executable(test_parse_url test_parse_url.c)
target_link_libraries(test_parse_url rabbitmq)
add_test(parse_url test_parse_url)

add_executable(test_tables test_tables.c)
target_link_libraries(test_tables rabbitmq)
add_test(tables test_tables)
set_tests_properties(tables PROPERTIES ENVIRONMENT srcdir=${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_hashtable test_hashtable.cpp)
target_link_libraries(test_hashtable gtest_main gtest)
add_test(hashtable test_hashtable)

add_executable(test_pools test_pools.cpp)
target_link_libraries(test_pools rabbitmq gtest_main gtest)
add_test(pools test_pools)
