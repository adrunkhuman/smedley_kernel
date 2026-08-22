#include <smedley/game_state/province_modifier_list.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace detail = smedley::game_state::detail;

namespace
{
    class Memory
    {
    public:
        template <typename T>
        void Set(uintptr_t address, T value)
        {
            std::vector<uint8_t> bytes(sizeof(value));
            std::memcpy(bytes.data(), &value, sizeof(value));
            values_[address] = std::move(bytes);
        }

        bool Read(uintptr_t address, void *output, size_t size) const
        {
            const auto found = values_.find(address);
            if (found == values_.end() || output == nullptr || found->second.size() != size) return false;
            std::memcpy(output, found->second.data(), size);
            return true;
        }

    private:
        std::unordered_map<uintptr_t, std::vector<uint8_t>> values_;
    };

    constexpr uintptr_t province = 0x1000;
    constexpr uintptr_t first_node = 0x2000;
    constexpr uintptr_t second_node = 0x2100;
    constexpr uintptr_t first_modifier = 0x3000;
    constexpr uintptr_t second_modifier = 0x3100;
    constexpr uintptr_t first_definition = 0x4000;
    constexpr uintptr_t second_definition = 0x4100;

    Memory ValidList()
    {
        Memory memory;
        memory.Set(province + 0xc8, first_node);
        memory.Set(province + 0xcc, second_node);
        memory.Set<int32_t>(province + 0xd0, 2);
        memory.Set<uint8_t>(province + 0xd4, 7);
        memory.Set(first_node, first_modifier);
        memory.Set<uintptr_t>(first_node + 0x4, 0);
        memory.Set(first_node + 0x8, second_node);
        memory.Set(second_node, second_modifier);
        memory.Set(second_node + 0x4, first_node);
        memory.Set<uintptr_t>(second_node + 0x8, 0);
        memory.Set(first_modifier + 0x8, first_definition);
        memory.Set(second_modifier + 0x8, second_definition);
        return memory;
    }

    bool ReadList(const Memory &memory, uint32_t *mask, uint32_t *guard)
    {
        const uintptr_t definitions[] = {first_definition, second_definition};
        return detail::ReadProvinceModifierMask(province, definitions, 2,
            [&](uintptr_t address, void *output, size_t size) { return memory.Read(address, output, size); },
            mask, guard);
    }
}

TEST(ProvinceModifierListTest, ReadsValidatedMaskAndGuard)
{
    const auto memory = ValidList();
    uint32_t mask = 0;
    uint32_t guard = 0;

    EXPECT_TRUE(ReadList(memory, &mask, &guard));
    EXPECT_EQ(mask, 3u);
    EXPECT_EQ(guard, 7u);
}

TEST(ProvinceModifierListTest, RejectsBrokenPreviousLink)
{
    auto memory = ValidList();
    memory.Set<uintptr_t>(second_node + 0x4, 0);
    uint32_t mask = 99;
    uint32_t guard = 99;

    EXPECT_FALSE(ReadList(memory, &mask, &guard));
    EXPECT_EQ(mask, 99u);
    EXPECT_EQ(guard, 99u);
}

TEST(ProvinceModifierListTest, RejectsTailOrCountMismatch)
{
    auto memory = ValidList();
    memory.Set(province + 0xcc, first_node);
    uint32_t mask = 0;
    uint32_t guard = 0;
    EXPECT_FALSE(ReadList(memory, &mask, &guard));

    memory = ValidList();
    memory.Set<int32_t>(province + 0xd0, 1);
    EXPECT_FALSE(ReadList(memory, &mask, &guard));
}
