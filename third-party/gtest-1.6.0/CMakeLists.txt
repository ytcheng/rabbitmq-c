project(gtest CXX)

include(cmake/internal_utils.cmake)

config_compiler_and_linker()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

cxx_library(gtest "${cxx_strict}" gtest/gtest-all.cc)
cxx_library(gtest_main "${cxx_strict}" gtest/gtest_main.cc)

target_link_libraries(gtest_main gtest)
