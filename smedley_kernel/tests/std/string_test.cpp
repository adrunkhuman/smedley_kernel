#include "memory.hpp"
#include "std/string.hpp"

#include <gtest/gtest.h>
#include <windows.h>

TEST(SStdStringTest, TerminatesInlineAndHeapStrings)
{
    smedley::memory::Map::game_heap = GetProcessHeap();

    const smedley::sstd::string inline_string("ENG");
    ASSERT_EQ(inline_string.size(), 3);
    ASSERT_STREQ(inline_string.c_str(), "ENG");

    const smedley::sstd::string heap_string("0123456789abcdef");
    ASSERT_EQ(heap_string.size(), 16);
    ASSERT_STREQ(heap_string.c_str(), "0123456789abcdef");
}

TEST(SStdStringTest, DifferentLengthsAreNotEqual)
{
    smedley::memory::Map::game_heap = GetProcessHeap();

    const smedley::sstd::string short_string("a");
    const smedley::sstd::string long_string("abcdefghijklmnopq");
    ASSERT_FALSE(short_string == long_string);
}
