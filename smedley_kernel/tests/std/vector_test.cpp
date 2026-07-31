#include "memory.hpp"
#include "std/vector.hpp"
#include <gtest/gtest.h>
#include <iostream>
#include <windows.h>

using namespace smedley;

TEST(SStdVectorTest, TestPushBack)
{
    memory::Map::game_heap = GetProcessHeap();

    sstd::vector<int> test_vec;
    test_vec.push_back(1);
    test_vec.push_back(2);

    ASSERT_EQ(test_vec.size(), 2);
    ASSERT_EQ(test_vec[0], 1);
    ASSERT_EQ(test_vec[1], 2);
}

TEST(SStdVectorTest, EraseValue)
{
    memory::Map::game_heap = GetProcessHeap();

    sstd::vector<int> values;
    values.push_back(1);
    values.push_back(2);
    values.push_back(3);

    ASSERT_TRUE(values.erase_value(2));
    ASSERT_EQ(values.size(), 2);
    ASSERT_EQ(values[0], 1);
    ASSERT_EQ(values[1], 3);
    ASSERT_FALSE(values.erase_value(2));
}

