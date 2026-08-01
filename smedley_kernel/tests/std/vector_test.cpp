#include "memory.hpp"
#include "clausewitz/types.hpp"
#include "std/vector.hpp"
#include "v2/tag.hpp"
#include <gtest/gtest.h>
#include <iostream>
#include <windows.h>

using namespace smedley;

namespace
{
    class MalformedVector : public sstd::vector<int>
    {
    public:
        void Set(uintptr_t first, uintptr_t last, uintptr_t end)
        {
            _first = reinterpret_cast<int *>(first);
            _last = reinterpret_cast<int *>(last);
            _end = reinterpret_cast<int *>(end);
        }
    };

    class CandidateList : public clausewitz::CList<int>
    {
    public:
        void Set(Node *head, Node *tail, int size)
        {
            _head = head;
            _tail = tail;
            _size = size;
        }
    };
}

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

TEST(SStdVectorTest, ValidatesCandidateMetadataWithoutPointerSubtraction)
{
    size_t count = 99;
    MalformedVector values;
    ASSERT_TRUE(values.bounded_size(10, &count));
    EXPECT_EQ(count, 0u);

    values.Set(0x2000, 0x1000, 0x3000);
    EXPECT_FALSE(values.bounded_size(10, &count));
    values.Set(0x1000, 0x1011, 0x2000);
    EXPECT_FALSE(values.bounded_size(10, &count));
    values.Set(0x1000, 0x1040, 0x2000);
    EXPECT_FALSE(values.bounded_size(10, &count));
}

TEST(ClausewitzListTest, ValidatesCandidateMetadata)
{
    int count = 99;
    CandidateList list;
    list.Set(nullptr, nullptr, 0);
    ASSERT_TRUE(list.bounded_size(10, &count));
    EXPECT_EQ(count, 0);

    CandidateList::Node node{};
    list.Set(&node, &node, 1);
    ASSERT_TRUE(list.bounded_size(10, &count));
    EXPECT_EQ(count, 1);
    list.Set(nullptr, nullptr, 1);
    EXPECT_FALSE(list.bounded_size(10, &count));
    list.Set(&node, &node, -1);
    EXPECT_FALSE(list.bounded_size(10, &count));
}

TEST(CountryTagTest, AcceptsNormalizedNumericAndSentinelTags)
{
    EXPECT_TRUE(v2::CCountryTag("PRU", 1).normalized_candidate());
    EXPECT_TRUE(v2::CCountryTag("D01", 2).normalized_candidate());
    EXPECT_TRUE(v2::CCountryTag("---", 0).normalized_candidate());
    EXPECT_FALSE(v2::CCountryTag("d01", 2).normalized_candidate());
}

