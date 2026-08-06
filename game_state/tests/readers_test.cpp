#include <smedley/game_state/readers.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace smedley::game_state
{
    namespace
    {
        template <typename T, size_t Size>
        void Write(std::array<std::byte, Size> *bytes, size_t offset, const T &value)
        {
            ASSERT_LE(offset + sizeof(value), bytes->size());
            std::memcpy(bytes->data() + offset, &value, sizeof(value));
        }

        template <size_t Size>
        void WriteInlineString(std::array<std::byte, Size> *bytes, size_t offset, const char *value)
        {
            const auto length = static_cast<uint32_t>(std::strlen(value));
            ASSERT_LT(length, 16u);
            ASSERT_LE(offset + 0x1c, bytes->size());
            std::memcpy(bytes->data() + offset, value, length + 1);
            Write(bytes, offset + 0x10, length);
            Write(bytes, offset + 0x14, uint32_t{15});
        }

        template <size_t Size>
        void WritePointerVector(std::array<std::byte, Size> *bytes, size_t offset,
                                const void *begin, const void *end, const void *capacity)
        {
            Write(bytes, offset, begin);
            Write(bytes, offset + sizeof(begin), end);
            Write(bytes, offset + 2 * sizeof(begin), capacity);
        }

        struct Node
        {
            const void *data;
            const Node *previous;
            const Node *next;
            uint8_t deleted;
            uint8_t padding[3];
        };

        struct PopList
        {
            const void *first;
            const void *last;
            int32_t count;
            uint32_t unknown;
        };

        struct CountryLookup
        {
            int32_t ordinal;
            const void *country;
            int32_t province_id = -1;
            const void *province = nullptr;
        };

        struct CountryLookupTable
        {
            const CountryLookup *entries;
            size_t count;
        };

        struct CreditorDestination
        {
            std::array<std::byte, 0xe9c> country{};
            std::array<std::byte, 0x28> bank{};
        };

        struct FactoryNode
        {
            std::array<std::byte, 0x220> data{};
            const FactoryNode *previous = nullptr;
            const FactoryNode *next = nullptr;
            uint8_t deleted = 0;
            uint8_t padding[3]{};
        };

        CountryRef Country(const void *pointer) { return CountryRef{pointer}; }
        ProvinceRef Province(const void *pointer) { return ProvinceRef{pointer}; }
        PopRef Pop(const void *pointer) { return PopRef{pointer}; }
        FactoryRef Factory(const void *pointer) { return FactoryRef{pointer}; }
        GameStateRef GameState(const void *pointer) { return GameStateRef{pointer}; }
        EmploymentRegistryRef EmploymentRegistry(const void *pointer) { return EmploymentRegistryRef{pointer}; }

        static_assert(sizeof(CountryRef) == sizeof(const void *));
        static_assert(sizeof(ProvinceRef) == sizeof(const void *));
        static_assert(sizeof(PopRef) == sizeof(const void *));
        static_assert(sizeof(FactoryRef) == sizeof(const void *));
        static_assert(sizeof(GameStateRef) == sizeof(const void *));
        static_assert(sizeof(EmploymentRegistryRef) == sizeof(const void *));
        static_assert(std::is_trivially_copyable_v<CountryRef>);
        static_assert(std::is_trivially_copyable_v<ProvinceRef>);
        static_assert(std::is_trivially_copyable_v<PopRef>);
        static_assert(std::is_trivially_copyable_v<FactoryRef>);
        static_assert(std::is_trivially_copyable_v<GameStateRef>);
        static_assert(std::is_trivially_copyable_v<EmploymentRegistryRef>);
        static_assert(!std::is_convertible_v<const void *, CountryRef>);
        static_assert(!std::is_convertible_v<const void *, ProvinceRef>);
        static_assert(!std::is_convertible_v<const void *, PopRef>);
        static_assert(!std::is_convertible_v<const void *, FactoryRef>);
        static_assert(!std::is_convertible_v<const void *, GameStateRef>);
        static_assert(!std::is_convertible_v<const void *, EmploymentRegistryRef>);
        static_assert(!std::is_convertible_v<CountryRef, ProvinceRef>);
        static_assert(!std::is_convertible_v<ProvinceRef, PopRef>);
        static_assert(!std::is_convertible_v<PopRef, FactoryRef>);
        static_assert(!std::is_convertible_v<FactoryRef, GameStateRef>);
        static_assert(!std::is_convertible_v<GameStateRef, EmploymentRegistryRef>);

        TEST(GameStateReferencesTest, SupportsNullAndSyntheticAddresses)
        {
            CountryRef empty{};
            EXPECT_FALSE(empty);
            EXPECT_EQ(empty.address(), 0u);

            std::array<std::byte, 1> bytes{};
            const PopRef pop{static_cast<const void *>(bytes.data())};
            EXPECT_TRUE(pop);
            EXPECT_EQ(pop.address(), reinterpret_cast<uintptr_t>(bytes.data()));
        }

        CountryRef ResolveCountry(const void *context, int32_t ordinal)
        {
            const auto *lookup = static_cast<const CountryLookup *>(context);
            return ordinal == lookup->ordinal ? Country(lookup->country) : CountryRef{};
        }

        ProvinceRef ResolveProvince(const void *context, int32_t id)
        {
            const auto *lookup = static_cast<const CountryLookup *>(context);
            return id == lookup->province_id ? Province(lookup->province) : ProvinceRef{};
        }

        CountryRef ResolveCountryFromTable(const void *context, int32_t ordinal)
        {
            const auto *table = static_cast<const CountryLookupTable *>(context);
            for (size_t index = 0; index < table->count; ++index) {
                if (table->entries[index].ordinal == ordinal) return Country(table->entries[index].country);
            }
            return {};
        }

        ProvinceRef ResolveProvinceFromTable(const void *context, int32_t id)
        {
            const auto *table = static_cast<const CountryLookupTable *>(context);
            for (size_t index = 0; index < table->count; ++index) {
                if (table->entries[index].province_id == id) return Province(table->entries[index].province);
            }
            return {};
        }

        CountryEconomySnapshot ReadResolvedCreditorDestinations(const std::vector<int32_t> &ordinals)
        {
            std::array<std::byte, 0xe9c> debtor{};
            std::array<std::byte, 0x28> debtor_bank{};
            std::vector<CreditorDestination> destinations(ordinals.size());
            std::vector<std::array<std::byte, 0x28>> creditors(ordinals.size());
            std::vector<void *> creditor_pointers;
            std::vector<CountryLookup> lookup_entries;
            const char debtor_tag[4] = {'S', 'W', 'E', '\0'};
            const char destination_tag[4] = {'E', 'N', 'G', '\0'};
            const void *debtor_bank_pointer = debtor_bank.data();

            creditor_pointers.reserve(creditors.size());
            lookup_entries.reserve(destinations.size());
            for (size_t index = 0; index < ordinals.size(); ++index) {
                const int32_t ordinal = ordinals[index];
                const int64_t bank_interest = static_cast<int64_t>(ordinal) * 10;
                const void *destination_bank = destinations[index].bank.data();
                Write(&destinations[index].country, 0x1c, destination_tag);
                Write(&destinations[index].country, 0x20, ordinal);
                Write(&destinations[index].country, 0xe88, destination_bank);
                Write(&destinations[index].bank, 0x20, bank_interest);
                Write(&creditors[index], 0x08, destination_tag);
                Write(&creditors[index], 0x0c, ordinal);
                creditor_pointers.push_back(creditors[index].data());
                lookup_entries.push_back({ordinal, destinations[index].country.data()});
            }

            const void *creditor_begin = creditor_pointers.data();
            const void *creditor_end = creditor_pointers.data() + creditor_pointers.size();
            Write(&debtor, 0x1c, debtor_tag);
            Write(&debtor, 0xe88, debtor_bank_pointer);
            Write(&debtor, 0xe8c, creditor_begin);
            Write(&debtor, 0xe90, creditor_end);
            Write(&debtor, 0xe94, creditor_end);
            const CountryLookupTable table{lookup_entries.data(), lookup_entries.size()};
            return ReadCountryCreditors(Country(debtor.data()), 1234, ResolveCountryFromTable, &table);
        }
    }

    TEST(GameStateReadersTest, ReadsCheckedGameStateValuesAndReferences)
    {
        std::array<std::byte, 0xb10> game_state{};
        std::array<std::byte, 1> country{};
        std::array<std::byte, 1> province{};
        std::array<void *, 2> countries{nullptr, country.data()};
        std::array<void *, 3> provinces{nullptr, nullptr, province.data()};
        const void *country_begin = countries.data();
        const void *country_end = countries.data() + countries.size();
        const void *province_begin = provinces.data();
        const void *province_end = provinces.data() + provinces.size();

        WritePointerVector(&game_state, 0xacc, province_begin, province_end, province_end);
        WritePointerVector(&game_state, 0xadc, country_begin, country_end, country_end);
        Write(&game_state, 0xb0c, int32_t{1234});

        int32_t date_raw = 0;
        uint32_t country_count = 0;
        ASSERT_TRUE(ReadCurrentDate(GameState(game_state.data()), &date_raw));
        EXPECT_EQ(date_raw, 1234);
        ASSERT_TRUE(ReadCountryCount(GameState(game_state.data()), &country_count));
        EXPECT_EQ(country_count, countries.size());
        EXPECT_EQ(smedley::game_state::ResolveCountry(GameState(game_state.data()), 1).address(),
            reinterpret_cast<uintptr_t>(country.data()));
        EXPECT_EQ(smedley::game_state::ResolveProvince(GameState(game_state.data()), 2).address(),
            reinterpret_cast<uintptr_t>(province.data()));

        Write(&game_state, 0xb0c, int32_t{-42});
        ASSERT_TRUE(ReadCurrentDate(GameState(game_state.data()), &date_raw));
        EXPECT_EQ(date_raw, -42);
    }

    TEST(GameStateReadersTest, RejectsNullGameStateArgumentsAndNullElements)
    {
        std::array<std::byte, 0xb10> game_state{};
        std::array<void *, 1> countries{nullptr};
        std::array<void *, 1> provinces{nullptr};
        const void *country_begin = countries.data();
        const void *country_end = countries.data() + countries.size();
        const void *province_begin = provinces.data();
        const void *province_end = provinces.data() + provinces.size();
        WritePointerVector(&game_state, 0xacc, province_begin, province_end, province_end);
        WritePointerVector(&game_state, 0xadc, country_begin, country_end, country_end);

        int32_t date_raw = 0;
        uint32_t country_count = 0;
        EXPECT_FALSE(ReadCurrentDate({}, &date_raw));
        EXPECT_FALSE(ReadCurrentDate(GameState(game_state.data()), nullptr));
        EXPECT_FALSE(ReadCountryCount({}, &country_count));
        EXPECT_FALSE(ReadCountryCount(GameState(game_state.data()), nullptr));
        EXPECT_FALSE(smedley::game_state::ResolveCountry({}, 0));
        EXPECT_FALSE(smedley::game_state::ResolveProvince({}, 0));
        EXPECT_FALSE(smedley::game_state::ResolveCountry(GameState(game_state.data()), 0));
        EXPECT_FALSE(smedley::game_state::ResolveProvince(GameState(game_state.data()), 0));
    }

    TEST(GameStateReadersTest, RejectsMalformedGameStateVectorsAndInvalidIndices)
    {
        std::array<std::byte, 0xb10> game_state{};
        std::array<void *, 2> countries{};
        std::array<void *, 2> provinces{};
        const void *country_begin = countries.data();
        const void *country_end = countries.data() + countries.size();
        const void *province_begin = provinces.data();
        const void *province_end = provinces.data() + provinces.size();
        uint32_t country_count = 99;

        WritePointerVector(&game_state, 0xadc, country_end, country_begin, country_end);
        EXPECT_FALSE(ReadCountryCount(GameState(game_state.data()), &country_count));
        EXPECT_EQ(country_count, 99u);
        EXPECT_FALSE(smedley::game_state::ResolveCountry(GameState(game_state.data()), 0));

        WritePointerVector(&game_state, 0xadc, country_begin, country_end, countries.data() + 1);
        EXPECT_FALSE(ReadCountryCount(GameState(game_state.data()), &country_count));
        EXPECT_EQ(country_count, 99u);
        EXPECT_FALSE(smedley::game_state::ResolveCountry(GameState(game_state.data()), 0));

        const auto *misaligned_province_begin = reinterpret_cast<const std::byte *>(province_begin) + 1;
        const auto *misaligned_province_end = misaligned_province_begin + sizeof(void *);
        WritePointerVector(&game_state, 0xacc, misaligned_province_begin,
            misaligned_province_end, misaligned_province_end);
        EXPECT_FALSE(smedley::game_state::ResolveProvince(GameState(game_state.data()), 0));

        WritePointerVector(&game_state, 0xacc, province_begin, province_end, provinces.data() + 1);
        EXPECT_FALSE(smedley::game_state::ResolveProvince(GameState(game_state.data()), 0));

        WritePointerVector(&game_state, 0xacc, province_begin, province_end, province_end);
        WritePointerVector(&game_state, 0xadc, country_begin, country_end, country_end);
        EXPECT_FALSE(smedley::game_state::ResolveCountry(GameState(game_state.data()), -1));
        EXPECT_FALSE(smedley::game_state::ResolveCountry(GameState(game_state.data()), 2));
        EXPECT_FALSE(smedley::game_state::ResolveProvince(GameState(game_state.data()), -1));
        EXPECT_FALSE(smedley::game_state::ResolveProvince(GameState(game_state.data()), 2));
    }

    TEST(GameStateReadersTest, HonorsGameStateReferenceLimits)
    {
        std::array<std::byte, 0xb10> game_state{};
        std::array<std::byte, 1> country{};
        std::array<std::byte, 1> province{};
        std::array<void *, max_game_countries> countries{};
        std::array<void *, 4096> provinces{};
        countries.back() = country.data();
        provinces.back() = province.data();
        const void *country_begin = countries.data();
        const void *country_end = countries.data() + countries.size();
        const void *province_begin = provinces.data();
        const void *province_end = provinces.data() + provinces.size();
        WritePointerVector(&game_state, 0xacc, province_begin, province_end, province_end);
        WritePointerVector(&game_state, 0xadc, country_begin, country_end, country_end);

        uint32_t country_count = 0;
        ASSERT_TRUE(ReadCountryCount(GameState(game_state.data()), &country_count));
        EXPECT_EQ(country_count, max_game_countries);
        EXPECT_EQ(smedley::game_state::ResolveCountry(GameState(game_state.data()), max_game_countries - 1).address(),
            reinterpret_cast<uintptr_t>(country.data()));
        EXPECT_EQ(smedley::game_state::ResolveProvince(GameState(game_state.data()), 4095).address(),
            reinterpret_cast<uintptr_t>(province.data()));

        std::array<void *, max_game_countries + 1> too_many_countries{};
        const void *too_many_country_begin = too_many_countries.data();
        const void *too_many_country_end = too_many_countries.data() + too_many_countries.size();
        WritePointerVector(&game_state, 0xadc, too_many_country_begin, too_many_country_end, too_many_country_end);
        EXPECT_FALSE(ReadCountryCount(GameState(game_state.data()), &country_count));
        EXPECT_FALSE(smedley::game_state::ResolveCountry(GameState(game_state.data()), 0));

        std::array<void *, 4097> too_many_provinces{};
        const void *too_many_province_begin = too_many_provinces.data();
        const void *too_many_province_end = too_many_provinces.data() + too_many_provinces.size();
        WritePointerVector(&game_state, 0xacc, too_many_province_begin, too_many_province_end, too_many_province_end);
        EXPECT_FALSE(smedley::game_state::ResolveProvince(GameState(game_state.data()), 0));
    }

    TEST(GameStateReadersTest, CollectsBoundedStateAndBankCandidates)
    {
        std::array<std::byte, 0x1608> country{};
        std::array<std::byte, 0x290> state{};
        std::array<std::byte, 0x28> bank{};
        std::array<std::byte, 0x28> creditor_a{};
        std::array<std::byte, 0x28> creditor_b{};
        std::array<int, 2> provinces{7, 11};
        std::array<void *, 2> creditors{creditor_a.data(), creditor_b.data()};
        Node node{state.data(), nullptr, nullptr, 0, {}};

        const char tag[4] = {'E', 'N', 'G', '\0'};
        const int state_count = 1;
        const int64_t treasury = 90;
        const int64_t savings = 120;
        const int64_t interest = 30;
        const int64_t bank_interest = 40;
        const char creditor_a_tag[4] = {'F', 'R', 'A', '\0'};
        const char creditor_b_tag[4] = {'P', 'R', 'U', '\0'};
        const int32_t creditor_a_ordinal = 2;
        const int32_t creditor_b_ordinal = 3;
        const int64_t creditor_a_interest = 5;
        const int64_t creditor_b_interest = 7;
        const int64_t creditor_a_debt = 20;
        const int64_t creditor_b_debt = 30;
        const uint8_t creditor_a_paid = 1;
        const uint8_t creditor_b_paid = 0;
        const void *state_head = &node;
        const void *state_tail = &node;
        const void *bank_pointer = bank.data();
        const void *province_begin = provinces.data();
        const void *province_end = provinces.data() + provinces.size();
        const void *creditor_begin = creditors.data();
        const void *creditor_end = creditors.data() + creditors.size();

        Write(&country, 0x1c, tag);
        Write(&country, 0xe44, state_head);
        Write(&country, 0xe48, state_tail);
        Write(&country, 0xe4c, state_count);
        Write(&country, 0xe78, treasury);
        Write(&country, 0xe88, bank_pointer);
        Write(&country, 0xe8c, creditor_begin);
        Write(&country, 0xe90, creditor_end);
        Write(&country, 0xe94, creditor_end);
        Write(&state, 0x48, province_begin);
        Write(&state, 0x4c, province_end);
        Write(&state, 0x50, province_end);
        Write(&state, 0x258, savings);
        Write(&state, 0x260, interest);
        Write(&bank, 0x20, bank_interest);
        Write(&creditor_a, 0x08, creditor_a_tag);
        Write(&creditor_a, 0x0c, creditor_a_ordinal);
        Write(&creditor_a, 0x10, creditor_a_interest);
        Write(&creditor_a, 0x18, creditor_a_debt);
        Write(&creditor_a, 0x20, creditor_a_paid);
        Write(&creditor_b, 0x08, creditor_b_tag);
        Write(&creditor_b, 0x0c, creditor_b_ordinal);
        Write(&creditor_b, 0x10, creditor_b_interest);
        Write(&creditor_b, 0x18, creditor_b_debt);
        Write(&creditor_b, 0x20, creditor_b_paid);

        const auto sample = ReadCountryEconomy(Country(country.data()), 1234);
        EXPECT_STREQ(sample.country_tag, "ENG");
        EXPECT_EQ(sample.date_raw, 1234);
        EXPECT_EQ(sample.state_count_reported, 1);
        EXPECT_EQ(sample.states_walked, 1u);
        EXPECT_EQ(sample.province_element_candidates, 2u);
        EXPECT_EQ(sample.states_with_savings, 1u);
        EXPECT_EQ(sample.states_with_interest, 1u);
        EXPECT_EQ(sample.creditor_count, 2u);
        EXPECT_EQ(sample.creditors_was_paid, 1u);
        EXPECT_EQ(sample.creditor_interest_raw, creditor_a_interest + creditor_b_interest);
        EXPECT_EQ(sample.creditor_debt_raw, creditor_a_debt + creditor_b_debt);
        EXPECT_EQ(sample.treasury_raw, treasury);
        EXPECT_EQ(sample.state_savings_raw, savings);
        EXPECT_EQ(sample.state_interest_raw, interest);
        EXPECT_EQ(sample.bank_interest_raw, bank_interest);
        EXPECT_EQ(sample.flags, 0u);
    }

    TEST(GameStateReadersTest, CollectsCreditorAndDestinationCandidates)
    {
        std::array<std::byte, 0x1608> debtor{};
        std::array<std::byte, 0x1608> destination{};
        std::array<std::byte, 0x28> creditor{};
        std::array<std::byte, 0x28> debtor_bank{};
        std::array<std::byte, 0x28> destination_bank{};
        std::array<std::byte, 0x290> destination_state{};
        std::array<std::byte, 0x1a0> destination_province{};
        std::array<std::byte, 0x288> destination_pop{};
        std::array<void *, 1> creditors{creditor.data()};
        std::array<int32_t, 1> province_ids{3};
        std::array<PopList, 1> pop_lists{{{destination_pop.data(), destination_pop.data(), 1, 0}}};
        Node destination_node{destination_state.data(), nullptr, nullptr, 0, {}};
        const char debtor_tag[4] = {'S', 'W', 'E', '\0'};
        const char destination_tag[4] = {'E', 'N', 'G', '\0'};
        const int32_t destination_ordinal = 7;
        const int64_t creditor_interest = 15;
        const int64_t creditor_debt = 90;
        const uint8_t was_paid = 1;
        const int64_t destination_bank_interest = 40;
        const int64_t destination_state_savings = 120;
        const int64_t destination_state_interest = 30;
        const int64_t destination_pop_money = 5000;
        const int64_t destination_pop_interest_cash_flow = 40;
        const int64_t destination_pop_total_cash_flow = 100;
        const int64_t destination_pop_savings = 120000;
        const void *debtor_bank_pointer = debtor_bank.data();
        const void *destination_bank_pointer = destination_bank.data();
        const void *destination_state_head = &destination_node;
        const int destination_state_count = 1;
        const void *creditor_begin = creditors.data();
        const void *creditor_end = creditors.data() + creditors.size();
        const void *province_begin = province_ids.data();
        const void *province_end = province_ids.data() + province_ids.size();
        const void *pop_list_begin = pop_lists.data();
        const void *pop_list_end = pop_lists.data() + pop_lists.size();

        Write(&debtor, 0x1c, debtor_tag);
        Write(&debtor, 0xe88, debtor_bank_pointer);
        Write(&debtor, 0xe8c, creditor_begin);
        Write(&debtor, 0xe90, creditor_end);
        Write(&debtor, 0xe94, creditor_end);
        Write(&destination, 0x1c, destination_tag);
        Write(&destination, 0x20, destination_ordinal);
        Write(&destination, 0xe44, destination_state_head);
        Write(&destination, 0xe48, destination_state_head);
        Write(&destination, 0xe4c, destination_state_count);
        Write(&destination, 0xe88, destination_bank_pointer);
        Write(&destination_bank, 0x20, destination_bank_interest);
        Write(&destination_state, 0x258, destination_state_savings);
        Write(&destination_state, 0x260, destination_state_interest);
        Write(&destination_state, 0x48, province_begin);
        Write(&destination_state, 0x4c, province_end);
        Write(&destination_state, 0x50, province_end);
        Write(&destination_province, 0x194, pop_list_begin);
        Write(&destination_province, 0x198, pop_list_end);
        Write(&destination_province, 0x19c, pop_list_end);
        Write(&destination_pop, 0x180, destination_pop_money);
        Write(&destination_pop, 0x210, destination_pop_interest_cash_flow);
        Write(&destination_pop, 0x218, destination_pop_total_cash_flow);
        Write(&destination_pop, 0x250, destination_pop_savings);
        Write(&creditor, 0x08, destination_tag);
        Write(&creditor, 0x0c, destination_ordinal);
        Write(&creditor, 0x10, creditor_interest);
        Write(&creditor, 0x18, creditor_debt);
        Write(&creditor, 0x20, was_paid);
        const CountryLookup lookup{destination_ordinal, destination.data(), province_ids[0], destination_province.data()};

        PopRef immediate_pop{};
        const auto sample = ReadCountryEconomy(
            Country(debtor.data()), 1234, ResolveCountry, ResolveProvince, &lookup, &immediate_pop);
        EXPECT_EQ(sample.creditor_count, 1u);
        EXPECT_EQ(sample.creditor_destinations, 1u);
        EXPECT_EQ(sample.creditors_was_paid, 1u);
        EXPECT_EQ(sample.creditor_interest_raw, creditor_interest);
        EXPECT_EQ(sample.creditor_debt_raw, creditor_debt);
        EXPECT_EQ(sample.destination_bank_interest_raw, destination_bank_interest);
        EXPECT_EQ(sample.destination_state_savings_raw, destination_state_savings);
        EXPECT_EQ(sample.destination_state_interest_raw, destination_state_interest);
        EXPECT_EQ(sample.destination_provinces_resolved, 1u);
        EXPECT_EQ(sample.destination_province_attempts, 1u);
        EXPECT_EQ(sample.destination_pop_lists, 1u);
        EXPECT_EQ(sample.destination_pops, 1u);
        EXPECT_EQ(sample.destination_pop_attempts, 1u);
        EXPECT_EQ(sample.destination_pop_savings_raw, destination_pop_savings);
        EXPECT_EQ(sample.destination_pop_savings_state_scale_raw, destination_state_savings);
        EXPECT_EQ(sample.flags, 0u);
        EXPECT_EQ(immediate_pop.address(), reinterpret_cast<uintptr_t>(destination_pop.data()));

        const auto aggregate_only = ReadCountryEconomy(Country(debtor.data()), 1234);
        EXPECT_EQ(aggregate_only.creditor_count, 1u);
        EXPECT_EQ(aggregate_only.creditors_was_paid, 1u);
        EXPECT_EQ(aggregate_only.creditor_interest_raw, creditor_interest);
        EXPECT_EQ(aggregate_only.creditor_debt_raw, creditor_debt);
        EXPECT_EQ(aggregate_only.creditor_destinations, 0u);
        EXPECT_EQ(aggregate_only.flags, 0u);

        PopMoneySnapshot snapshot{};
        ASSERT_TRUE(ReadPopMoneySnapshot(immediate_pop, &snapshot));
        EXPECT_EQ(snapshot.money_raw, destination_pop_money);
        EXPECT_EQ(snapshot.interest_cash_flow_raw, destination_pop_interest_cash_flow);
        EXPECT_EQ(snapshot.total_cash_flow_raw, destination_pop_total_cash_flow);
        EXPECT_EQ(snapshot.savings_raw, destination_pop_savings);
        EXPECT_FALSE(ReadPopMoneySnapshot({}, &snapshot));

        std::array<PopCandidate, 1> candidates{};
        uint32_t candidate_count = 0;
        CountryEconomySnapshot pop_quality{};
        ASSERT_TRUE(CollectCountryPops(Country(destination.data()), 1234, ResolveProvince, &lookup,
            candidates.data(), candidates.size(), max_sample_destination_provinces,
            &candidate_count, &pop_quality));
        EXPECT_EQ(candidate_count, 1u);
        EXPECT_EQ(candidates[0].address.address(), reinterpret_cast<uintptr_t>(destination_pop.data()));
        EXPECT_EQ(candidates[0].savings_raw, destination_pop_savings);
        EXPECT_EQ(pop_quality.state_savings_raw, 0);
        EXPECT_EQ(pop_quality.state_interest_raw, 0);
        EXPECT_EQ(pop_quality.bank_interest_raw, 0);
        EXPECT_EQ(pop_quality.destination_pop_savings_raw, 0);
        EXPECT_EQ(pop_quality.destination_pop_savings_state_scale_raw, 0);
        EXPECT_EQ(pop_quality.flags, 0u);

        pop_lists[0].count = 2;
        const auto mismatched = ReadCountryEconomy(
            Country(debtor.data()), 1234, ResolveCountry, ResolveProvince, &lookup);
        EXPECT_NE(mismatched.flags & SAMPLE_POP_LIST_INVALID, 0u);

        pop_lists[0].count = 100001;
        const auto limited = ReadCountryEconomy(
            Country(debtor.data()), 1234, ResolveCountry, ResolveProvince, &lookup);
        EXPECT_NE(limited.flags & SAMPLE_POP_LIMIT, 0u);

        pop_lists[0].count = 1;
        std::array<int32_t, 2> duplicate_province_ids{province_ids[0], province_ids[0]};
        const void *duplicate_begin = duplicate_province_ids.data();
        const void *duplicate_end = duplicate_province_ids.data() + duplicate_province_ids.size();
        Write(&destination_state, 0x48, duplicate_begin);
        Write(&destination_state, 0x4c, duplicate_end);
        Write(&destination_state, 0x50, duplicate_end);
        const auto duplicate = ReadCountryEconomy(
            Country(debtor.data()), 1234, ResolveCountry, ResolveProvince, &lookup);
        EXPECT_NE(duplicate.flags & SAMPLE_DUPLICATE_PROVINCE, 0u);
        EXPECT_NE(duplicate.flags & SAMPLE_DUPLICATE_POP, 0u);
        EXPECT_EQ(duplicate.flags & SAMPLE_CREDITOR_DESTINATION_INVALID, 0u);
        EXPECT_EQ(duplicate.creditor_destinations, 1u);
        EXPECT_EQ(duplicate.destination_bank_interest_raw, destination_bank_interest);
        EXPECT_EQ(duplicate.destination_state_savings_raw, destination_state_savings);
        EXPECT_EQ(duplicate.destination_pop_savings_raw, destination_pop_savings * 2);
    }

    TEST(GameStateReadersTest, DetectsSharedPopAcrossCreditorDestinationsAfterAggregation)
    {
        std::array<std::byte, 0x1608> debtor{};
        std::array<std::byte, 0x1608> england{};
        std::array<std::byte, 0x1608> france{};
        std::array<std::byte, 0x28> england_creditor{};
        std::array<std::byte, 0x28> france_creditor{};
        std::array<std::byte, 0x28> england_bank{};
        std::array<std::byte, 0x28> france_bank{};
        std::array<std::byte, 0x290> england_state{};
        std::array<std::byte, 0x290> france_state{};
        std::array<std::byte, 0x1a0> province{};
        std::array<std::byte, 0x288> pop{};
        std::array<void *, 2> creditors{england_creditor.data(), france_creditor.data()};
        std::array<int32_t, 1> province_ids{3};
        std::array<PopList, 1> pop_lists{{{pop.data(), pop.data(), 1, 0}}};
        Node england_node{england_state.data(), nullptr, nullptr, 0, {}};
        Node france_node{france_state.data(), nullptr, nullptr, 0, {}};
        const char debtor_tag[4] = {'S', 'W', 'E', '\0'};
        const char england_tag[4] = {'E', 'N', 'G', '\0'};
        const char france_tag[4] = {'F', 'R', 'A', '\0'};
        const void *creditor_begin = creditors.data();
        const void *creditor_end = creditors.data() + creditors.size();
        const void *province_begin = province_ids.data();
        const void *province_end = province_ids.data() + province_ids.size();
        const void *pop_list_begin = pop_lists.data();
        const void *pop_list_end = pop_lists.data() + pop_lists.size();

        Write(&debtor, 0x1c, debtor_tag);
        Write(&debtor, 0xe8c, creditor_begin);
        Write(&debtor, 0xe90, creditor_end);
        Write(&debtor, 0xe94, creditor_end);
        Write(&england, 0x1c, england_tag);
        Write(&england, 0x20, 7);
        Write(&france, 0x1c, france_tag);
        Write(&france, 0x20, 8);
        Write(&england, 0xe44, &england_node);
        Write(&england, 0xe48, &england_node);
        Write(&england, 0xe4c, 1);
        Write(&france, 0xe44, &france_node);
        Write(&france, 0xe48, &france_node);
        Write(&france, 0xe4c, 1);
        Write(&england, 0xe88, england_bank.data());
        Write(&france, 0xe88, france_bank.data());
        Write(&england_bank, 0x20, 40LL);
        Write(&france_bank, 0x20, 60LL);
        for (auto *state : {&england_state, &france_state}) {
            Write(state, 0x48, province_begin);
            Write(state, 0x4c, province_end);
            Write(state, 0x50, province_end);
        }
        Write(&province, 0x194, pop_list_begin);
        Write(&province, 0x198, pop_list_end);
        Write(&province, 0x19c, pop_list_end);
        Write(&pop, 0x250, 120000LL);
        Write(&england_creditor, 0x08, england_tag);
        Write(&england_creditor, 0x0c, 7);
        Write(&england_creditor, 0x10, 15LL);
        Write(&england_creditor, 0x18, 90LL);
        Write(&england_creditor, 0x20, static_cast<uint8_t>(1));
        Write(&france_creditor, 0x08, france_tag);
        Write(&france_creditor, 0x0c, 8);
        Write(&france_creditor, 0x10, 15LL);
        Write(&france_creditor, 0x18, 90LL);
        Write(&france_creditor, 0x20, static_cast<uint8_t>(1));
        const std::array<CountryLookup, 2> entries{{{7, england.data(), 3, province.data()},
            {8, france.data(), 3, province.data()}}};
        const CountryLookupTable lookup{entries.data(), entries.size()};

        const auto sample = ReadCountryEconomy(
            Country(debtor.data()), 1234, ResolveCountryFromTable, ResolveProvinceFromTable, &lookup);
        EXPECT_NE(sample.flags & SAMPLE_DUPLICATE_POP, 0u);
        EXPECT_EQ(sample.flags & SAMPLE_CREDITOR_DESTINATION_INVALID, 0u);
        EXPECT_EQ(sample.creditor_destinations, 2u);
        EXPECT_EQ(sample.destination_bank_interest_raw, 100);
        EXPECT_EQ(sample.destination_pop_savings_raw, 240000);
    }

    TEST(GameStateReadersTest, CollectsStateInterestPoolsWithStateScopedPops)
    {
        std::array<std::byte, 0xb10> game_state{};
        std::array<std::byte, 0x1608> country{};
        std::array<std::byte, 0x290> state{};
        std::array<std::byte, 0x1a0> province{};
        std::array<std::byte, 0x288> pop{};
        std::array<void *, 1> provinces{province.data()};
        std::array<int32_t, 1> province_ids{0};
        std::array<PopList, 1> pop_lists{{{pop.data(), pop.data(), 1, 0}}};
        Node node{state.data(), nullptr, nullptr, 0, {}};
        const void *state_node = &node;
        const int32_t state_count_reported = 1;
        const int32_t state_id = 41;
        const int64_t state_savings = 700;
        const int64_t state_interest = 23;
        const int64_t pop_savings = 9000;
        const char tag[4] = {'E', 'N', 'G', '\0'};
        const int32_t ordinal = 1;

        WritePointerVector(&game_state, 0xacc, provinces.data(), provinces.data() + 1, provinces.data() + 1);
        Write(&country, 0x1c, tag);
        Write(&country, 0x20, ordinal);
        Write(&country, 0xe44, state_node);
        Write(&country, 0xe48, state_node);
        Write(&country, 0xe4c, state_count_reported);
        Write(&state, 0x0c, state_id);
        WritePointerVector(&state, 0x48, province_ids.data(), province_ids.data() + 1, province_ids.data() + 1);
        Write(&state, 0x258, state_savings);
        Write(&state, 0x260, state_interest);
        WritePointerVector(&province, 0x194, pop_lists.data(), pop_lists.data() + 1, pop_lists.data() + 1);
        Write(&pop, 0x250, pop_savings);

        std::array<StateInterestCandidate, 2> states{};
        std::array<PopCandidate, 2> pops{};
        uint32_t state_count = 0;
        uint32_t pop_count = 0;
        CountryEconomySnapshot quality{};
        ASSERT_TRUE(CollectCountryStateInterest(Country(country.data()), GameState(game_state.data()), 1234,
            states.data(), states.size(), &state_count, pops.data(), pops.size(),
            max_sample_destination_provinces, &pop_count, &quality));
        ASSERT_EQ(state_count, 1u);
        ASSERT_EQ(pop_count, 1u);
        EXPECT_EQ(states[0].state.address(), reinterpret_cast<uintptr_t>(state.data()));
        EXPECT_EQ(states[0].state_id, state_id);
        EXPECT_EQ(states[0].savings_raw, state_savings);
        EXPECT_EQ(states[0].interest_raw, state_interest);
        EXPECT_EQ(states[0].first_pop_index, 0u);
        EXPECT_EQ(states[0].pop_count, 1u);
        EXPECT_EQ(states[0].province_count, 1u);
        EXPECT_EQ(pops[0].address.address(), reinterpret_cast<uintptr_t>(pop.data()));
        EXPECT_EQ(pops[0].savings_raw, pop_savings);
        EXPECT_EQ(quality.state_interest_raw, state_interest);
        EXPECT_EQ(quality.destination_province_attempts, 1u);
        EXPECT_EQ(quality.destination_pop_attempts, 1u);
        EXPECT_EQ(quality.flags, 0u);

        state_count = 0;
        pop_count = 0;
        ASSERT_TRUE(CollectCountryStateInterest(Country(country.data()), GameState(game_state.data()), 1234,
            states.data(), states.size(), &state_count, nullptr, 0, 0, &pop_count, &quality));
        EXPECT_EQ(state_count, 1u);
        EXPECT_EQ(pop_count, 0u);
        EXPECT_EQ(states[0].pop_count, 0u);
    }

    TEST(GameStateReadersTest, ReadsCreditorDestinationBankWithoutStateTraversal)
    {
        std::array<std::byte, 0xe9c> debtor{};
        std::array<std::byte, 0xe9c> destination{};
        std::array<std::byte, 0x28> creditor{};
        std::array<std::byte, 0x28> debtor_bank{};
        std::array<std::byte, 0x28> destination_bank{};
        std::array<void *, 1> creditors{creditor.data()};
        const char debtor_tag[4] = {'S', 'W', 'E', '\0'};
        const char destination_tag[4] = {'E', 'N', 'G', '\0'};
        const int32_t destination_ordinal = 7;
        const int64_t destination_bank_interest = 40;
        const void *debtor_bank_pointer = debtor_bank.data();
        const void *destination_bank_pointer = destination_bank.data();
        const void *creditor_begin = creditors.data();
        const void *creditor_end = creditors.data() + creditors.size();
        const void *invalid_state = reinterpret_cast<const void *>(1);

        Write(&debtor, 0x1c, debtor_tag);
        Write(&debtor, 0xe88, debtor_bank_pointer);
        Write(&debtor, 0xe8c, creditor_begin);
        Write(&debtor, 0xe90, creditor_end);
        Write(&debtor, 0xe94, creditor_end);
        Write(&destination, 0x1c, destination_tag);
        Write(&destination, 0x20, destination_ordinal);
        Write(&destination, 0xe44, invalid_state);
        Write(&destination, 0xe48, invalid_state);
        Write(&destination, 0xe4c, int32_t{1});
        Write(&destination, 0xe88, destination_bank_pointer);
        Write(&destination_bank, 0x20, destination_bank_interest);
        Write(&creditor, 0x08, destination_tag);
        Write(&creditor, 0x0c, destination_ordinal);
        const CountryLookup lookup{destination_ordinal, destination.data()};

        const auto sample = ReadCountryCreditors(Country(debtor.data()), 1234, ResolveCountry, &lookup);
        ASSERT_EQ(sample.creditor_destinations, 1u);
        EXPECT_EQ(sample.destination_ordinals[0], destination_ordinal);
        EXPECT_EQ(sample.destination_bank_interests_raw[0], destination_bank_interest);
        EXPECT_EQ(sample.destination_bank_interest_raw, destination_bank_interest);
        EXPECT_EQ(sample.flags, 0u);
    }

    TEST(GameStateReadersTest, DeduplicatesHashCollidingCreditorDestinations)
    {
        const auto sample = ReadResolvedCreditorDestinations({1, 1025, 1});

        ASSERT_EQ(sample.creditor_destinations, 2u);
        EXPECT_EQ(sample.destination_ordinals[0], 1);
        EXPECT_EQ(sample.destination_ordinals[1], 1025);
        EXPECT_EQ(sample.destination_bank_interests_raw[0], 10);
        EXPECT_EQ(sample.destination_bank_interests_raw[1], 10250);
        EXPECT_EQ(sample.destination_bank_interest_raw, 10260);
        EXPECT_NE(sample.flags & SAMPLE_CREDITOR_DUPLICATE_DESTINATION, 0u);
        EXPECT_EQ(sample.flags & SAMPLE_CREDITOR_DESTINATION_LIMIT, 0u);
    }

    TEST(GameStateReadersTest, AcceptsExactlyMaximumUniqueCreditorDestinations)
    {
        std::vector<int32_t> ordinals;
        ordinals.reserve(max_sample_creditor_destinations);
        for (uint32_t ordinal = 1; ordinal <= max_sample_creditor_destinations; ++ordinal) {
            ordinals.push_back(static_cast<int32_t>(ordinal));
        }

        const auto sample = ReadResolvedCreditorDestinations(ordinals);
        ASSERT_EQ(sample.creditor_destinations, max_sample_creditor_destinations);
        EXPECT_EQ(sample.flags & SAMPLE_CREDITOR_DESTINATION_LIMIT, 0u);
        for (uint32_t index = 0; index < max_sample_creditor_destinations; ++index) {
            EXPECT_EQ(sample.destination_ordinals[index], static_cast<int32_t>(index + 1));
            EXPECT_EQ(sample.destination_bank_interests_raw[index], static_cast<int64_t>(index + 1) * 10);
        }
    }

    TEST(GameStateReadersTest, LimitsThe513thUniqueCreditorDestinationWithoutOverwritingArrays)
    {
        std::vector<int32_t> ordinals;
        ordinals.reserve(max_sample_creditor_destinations + 1);
        for (uint32_t ordinal = 1; ordinal <= max_sample_creditor_destinations + 1; ++ordinal) {
            ordinals.push_back(static_cast<int32_t>(ordinal));
        }

        const auto sample = ReadResolvedCreditorDestinations(ordinals);
        ASSERT_EQ(sample.creditor_destinations, max_sample_creditor_destinations);
        EXPECT_NE(sample.flags & SAMPLE_CREDITOR_DESTINATION_LIMIT, 0u);
        for (uint32_t index = 0; index < max_sample_creditor_destinations; ++index) {
            EXPECT_EQ(sample.destination_ordinals[index], static_cast<int32_t>(index + 1));
            EXPECT_EQ(sample.destination_bank_interests_raw[index], static_cast<int64_t>(index + 1) * 10);
        }
    }

    TEST(GameStateReadersTest, RejectsSelfReferentialStateList)
    {
        std::array<std::byte, 0x1608> country{};
        std::array<std::byte, 0x290> state{};
        Node node{state.data(), nullptr, nullptr, 0, {}};
        node.next = &node;
        const void *head = &node;
        const int state_count = 2;
        Write(&country, 0xe44, head);
        Write(&country, 0xe48, head);
        Write(&country, 0xe4c, state_count);

        const auto sample = ReadCountryEconomy(Country(country.data()), 0);
        EXPECT_EQ(sample.states_walked, 1u);
        EXPECT_NE(sample.flags & SAMPLE_STATE_LIST_INVALID, 0u);
        EXPECT_NE(sample.flags & SAMPLE_STATE_COUNT_MISMATCH, 0u);
    }

    TEST(GameStateReadersTest, RejectsMalformedProvinceVector)
    {
        std::array<std::byte, 0x1608> country{};
        std::array<std::byte, 0x290> state{};
        std::array<int, 1> provinces{};
        Node node{state.data(), nullptr, nullptr, 0, {}};
        const void *head = &node;
        const int state_count = 1;
        const void *begin = provinces.data() + 1;
        const void *end = provinces.data();
        Write(&country, 0xe44, head);
        Write(&country, 0xe48, head);
        Write(&country, 0xe4c, state_count);
        Write(&state, 0x48, begin);
        Write(&state, 0x4c, end);
        Write(&state, 0x50, begin);

        const auto sample = ReadCountryEconomy(Country(country.data()), 0);
        EXPECT_NE(sample.flags & SAMPLE_STATE_VECTOR_INVALID, 0u);
    }

    TEST(GameStateReadersTest, AggregatesOpaqueCreditorsWithoutPollutingPopTraversal)
    {
        std::array<std::byte, 0x1608> country{};
        std::array<std::byte, 0x28> bank{};
        std::array<std::byte, 0x28> creditor{};
        std::array<void *, 1> creditors{creditor.data()};
        const int64_t interest = 15;
        const int64_t debt = 90;
        const uint8_t was_paid = 1;
        const char no_country_tag[4] = {'-', '-', '-', '\0'};
        const void *bank_pointer = bank.data();
        const void *creditor_begin = creditors.data();
        const void *creditor_end = creditors.data() + creditors.size();

        Write(&country, 0xe88, bank_pointer);
        Write(&country, 0xe8c, creditor_begin);
        Write(&country, 0xe90, creditor_end);
        Write(&country, 0xe94, creditor_end);
        Write(&creditor, 0x08, no_country_tag);
        Write(&creditor, 0x10, interest);
        Write(&creditor, 0x18, debt);
        Write(&creditor, 0x20, was_paid);

        const auto aggregate = ReadCountryEconomy(Country(country.data()), 1234);
        EXPECT_EQ(aggregate.creditor_count, 1u);
        EXPECT_EQ(aggregate.creditors_was_paid, 1u);
        EXPECT_EQ(aggregate.creditor_interest_raw, interest);
        EXPECT_EQ(aggregate.creditor_debt_raw, debt);
        EXPECT_EQ(aggregate.flags, 0u);

        const CountryLookup lookup{};
        const auto resolved = ReadCountryEconomy(Country(country.data()), 1234, ResolveCountry, nullptr, &lookup);
        EXPECT_EQ(resolved.creditor_count, 1u);
        EXPECT_EQ(resolved.creditor_destinations, 0u);
        EXPECT_EQ(resolved.creditors_was_paid, 1u);
        EXPECT_EQ(resolved.creditor_interest_raw, interest);
        EXPECT_EQ(resolved.creditor_debt_raw, debt);
        EXPECT_EQ(resolved.flags, 0u);

        const uint8_t invalid_paid = 2;
        Write(&creditor, 0x20, invalid_paid);
        const void *malformed_creditor_begin = creditors.data() + creditors.size();
        const void *malformed_creditor_end = creditors.data();
        Write(&country, 0xe8c, malformed_creditor_begin);
        Write(&country, 0xe90, malformed_creditor_end);
        uint32_t candidate_count = 0;
        CountryEconomySnapshot quality{};
        EXPECT_TRUE(CollectCountryPops(Country(country.data()), 1234, ResolveProvince, &lookup,
            nullptr, 0, max_sample_destination_provinces, &candidate_count, &quality));
        EXPECT_EQ(candidate_count, 0u);
        EXPECT_EQ(quality.creditor_count, 0u);
        EXPECT_EQ(quality.flags, 0u);
    }

    TEST(GameStateReadersTest, RejectsMismatchedCreditorDestination)
    {
        std::array<std::byte, 0x1608> debtor{};
        std::array<std::byte, 0x1608> destination{};
        std::array<std::byte, 0x28> creditor{};
        std::array<std::byte, 0x28> debtor_bank{};
        std::array<void *, 1> creditors{creditor.data()};
        const char debtor_tag[4] = {'S', 'W', 'E', '\0'};
        const char creditor_tag[4] = {'E', 'N', 'G', '\0'};
        const char destination_tag[4] = {'F', 'R', 'A', '\0'};
        const int32_t destination_ordinal = 7;
        const void *debtor_bank_pointer = debtor_bank.data();
        const void *creditor_begin = creditors.data();
        const void *creditor_end = creditors.data() + creditors.size();

        Write(&debtor, 0x1c, debtor_tag);
        Write(&debtor, 0xe88, debtor_bank_pointer);
        Write(&debtor, 0xe8c, creditor_begin);
        Write(&debtor, 0xe90, creditor_end);
        Write(&debtor, 0xe94, creditor_end);
        Write(&destination, 0x1c, destination_tag);
        Write(&destination, 0x20, destination_ordinal);
        Write(&creditor, 0x08, creditor_tag);
        Write(&creditor, 0x0c, destination_ordinal);
        const CountryLookup lookup{destination_ordinal, destination.data()};

        const auto sample = ReadCountryEconomy(Country(debtor.data()), 1234, ResolveCountry, nullptr, &lookup);
        EXPECT_EQ(sample.creditor_destinations, 0u);
        EXPECT_NE(sample.flags & SAMPLE_CREDITOR_DESTINATION_INVALID, 0u);
    }

    TEST(GameStateReadersTest, CollectsBoundedFactoryFields)
    {
        std::array<std::byte, 0x1608> country{};
        std::array<std::byte, 0x290> state{};
        std::array<std::byte, 0x108> region{};
        std::array<std::byte, 0x140> definition{};
        std::array<std::byte, 0x140> production_type{};
        std::array<std::byte, 0x40> output_good{};
        std::array<std::byte, 0x70> pop{};
        std::array<std::byte, 0x40> pop_type{};
        std::array<std::byte, 0x10> employment{};
        std::array<int32_t, 1> provinces{549};
        std::array<int64_t, 2> stockpile_values{0, 12357};
        std::array<int64_t, 3> requested_values{0, 6543, 7777};
        Node state_node{state.data(), nullptr, nullptr, 0, {}};
        FactoryNode factory_node{};
        const void *state_head = &state_node;
        const int state_count = 1;
        const void *province_begin = provinces.data();
        const void *province_end = provinces.data() + provinces.size();
        const void *factory_head = &factory_node;
        const int factory_count = 1;
        const void *definition_pointer = definition.data();
        const void *region_pointer = region.data();
        const void *production_type_pointer = production_type.data();
        const void *output_good_pointer = output_good.data();
        const void *pop_pointer = pop.data();
        const void *pop_type_pointer = pop_type.data();
        const void *employment_begin = employment.data();
        const void *employment_end = employment.data() + employment.size();
        const void *stockpile_begin = stockpile_values.data();
        const void *stockpile_end = stockpile_values.data() + stockpile_values.size();
        const void *requested_begin = requested_values.data();
        const void *requested_end = requested_values.data() + requested_values.size();
        const char factory_type[] = "glass_factory";
        const uint32_t factory_type_size = sizeof(factory_type) - 1;
        const uint32_t factory_type_capacity = 15;
        const char output_good_type[] = "small_arms";
        const uint32_t output_good_type_size = sizeof(output_good_type) - 1;
        const uint32_t output_good_type_capacity = 15;
        const char pop_type_name[] = "craftsmen";
        const uint32_t pop_type_name_size = sizeof(pop_type_name) - 1;
        const uint32_t pop_type_name_capacity = 15;
        const char state_region_key[] = "PRU_549";
        const uint32_t state_region_key_size = sizeof(state_region_key) - 1;
        const int32_t state_id = 750;
        const int32_t level = 1;
        const int32_t employees = 1998;
        const int32_t output = 13437;
        const int32_t output_good_ordinal = 1;
        const int32_t base_output = 65536;
        const int64_t budget = 8244550872;
        const int64_t spending = 362208000;
        const int64_t income = 497291000;
        const int64_t paychecks = 8504523;
        const int64_t investment = 804455000;

        Write(&country, 0xe44, state_head); Write(&country, 0xe48, state_head); Write(&country, 0xe4c, state_count);
        Write(&state, 0x48, province_begin); Write(&state, 0x4c, province_end); Write(&state, 0x50, province_end);
        Write(&state, 0x60, factory_head); Write(&state, 0x64, factory_head); Write(&state, 0x68, factory_count);
        Write(&state, 0x0c, state_id); Write(&state, 0x250, region_pointer);
        Write(&factory_node.data, 0x18, definition_pointer); Write(&factory_node.data, 0x20, level);
        Write(&factory_node.data, 0xd8, output); Write(&factory_node.data, 0x128, employees);
        Write(&factory_node.data, 0x150, budget); Write(&factory_node.data, 0x158, spending);
        Write(&factory_node.data, 0x160, income); Write(&factory_node.data, 0x168, paychecks); Write(&factory_node.data, 0x170, investment);
        factory_node.data[0x30] = std::byte{1}; factory_node.data[0x88] = std::byte{1};
        factory_node.data[0x89] = std::byte{2}; factory_node.data[0xb8] = std::byte{0xff};
        Write(&factory_node.data, 0x70, stockpile_begin); Write(&factory_node.data, 0x74, stockpile_end); Write(&factory_node.data, 0x78, stockpile_end);
        Write(&factory_node.data, 0xc8, requested_begin); Write(&factory_node.data, 0xcc, requested_end); Write(&factory_node.data, 0xd0, requested_end);
        Write(&factory_node.data, 0xf0, employment_begin); Write(&factory_node.data, 0xf4, employment_end); Write(&factory_node.data, 0xf8, employment_end);
        std::memcpy(definition.data() + 0x20, factory_type, sizeof(factory_type));
        Write(&definition, 0x30, factory_type_size); Write(&definition, 0x34, factory_type_capacity); Write(&definition, 0x12c, production_type_pointer);
        Write(&production_type, 0x80, output_good_pointer); Write(&production_type, 0x88, base_output);
        Write(&output_good, 0x08, output_good_ordinal); std::memcpy(output_good.data() + 0x0c, output_good_type, sizeof(output_good_type));
        Write(&output_good, 0x1c, output_good_type_size); Write(&output_good, 0x20, output_good_type_capacity);
        Write(&pop, 0x68, pop_type_pointer); std::memcpy(pop_type.data() + 0x08, pop_type_name, sizeof(pop_type_name));
        Write(&pop_type, 0x18, pop_type_name_size); Write(&pop_type, 0x1c, pop_type_name_capacity);
        Write(&employment, 0x08, pop_pointer); Write(&employment, 0x0c, employees);
        std::memcpy(region.data() + 0x18, state_region_key, sizeof(state_region_key));
        Write(&region, 0x28, state_region_key_size); Write(&region, 0x2c, factory_type_capacity);

        std::array<FactorySnapshot, 2> snapshots{};
        std::array<FactoryInputSnapshot, 2> inputs{};
        uint32_t captured = 0;
        uint32_t input_count = 0;
        uint32_t flags = 0;
        ASSERT_TRUE(CollectCountryFactories(Country(country.data()), snapshots.data(), snapshots.size(), &captured,
            inputs.data(), inputs.size(), &input_count,
            FACTORY_IDENTITY | FACTORY_EMPLOYMENT | FACTORY_PRODUCTION | FACTORY_FINANCE | FACTORY_INPUTS,
            &flags, 48));
        ASSERT_EQ(flags, 0u); ASSERT_EQ(captured, 1u);
        const auto &snapshot = snapshots[0];
        EXPECT_EQ(snapshot.state_index, 0u); EXPECT_EQ(snapshot.factory_index, 0u); EXPECT_EQ(snapshot.state_id, state_id);
        EXPECT_STREQ(snapshot.state_region_key, state_region_key); EXPECT_EQ(snapshot.anchor_province_id_candidate, 549);
        EXPECT_STREQ(snapshot.factory_type, factory_type); EXPECT_EQ(snapshot.level, level); EXPECT_EQ(snapshot.employee_count, employees);
        EXPECT_EQ(snapshot.craftsmen_count, employees); EXPECT_EQ(snapshot.clerk_count, 0); EXPECT_EQ(snapshot.output_raw, output);
        EXPECT_EQ(snapshot.output_good_ordinal, output_good_ordinal); EXPECT_STREQ(snapshot.output_good, output_good_type);
        EXPECT_EQ(snapshot.base_output_raw, base_output); EXPECT_FALSE(snapshot.subsidized); EXPECT_FALSE(snapshot.closed);
        EXPECT_EQ(snapshot.budget_raw, budget); EXPECT_EQ(snapshot.market_spending_raw, spending); EXPECT_EQ(snapshot.sales_income_raw, income);
        EXPECT_EQ(snapshot.paychecks_raw, paychecks); EXPECT_EQ(snapshot.investment_raw, investment);
        ASSERT_EQ(input_count, 2u); EXPECT_EQ(inputs[0].factory_snapshot_index, 0u); EXPECT_EQ(inputs[0].good_ordinal, 0);
        EXPECT_EQ(inputs[0].stockpile_raw, stockpile_values[1]); EXPECT_EQ(inputs[0].requested_raw, requested_values[1]);
        EXPECT_EQ(inputs[1].good_ordinal, 1); EXPECT_EQ(inputs[1].stockpile_raw, 0); EXPECT_EQ(inputs[1].requested_raw, requested_values[2]);

        const void *null_pointer = nullptr;
        Write(&definition, 0x12c, null_pointer);
        ASSERT_TRUE(CollectCountryFactories(Country(country.data()), snapshots.data(), snapshots.size(), &captured,
            inputs.data(), inputs.size(), &input_count, FACTORY_IDENTITY, &flags));
        EXPECT_EQ(captured, 1u); Write(&definition, 0x12c, production_type_pointer);
        Write(&state, 0x48, null_pointer); Write(&state, 0x4c, null_pointer); Write(&state, 0x50, null_pointer);
        ASSERT_TRUE(CollectCountryFactories(Country(country.data()), snapshots.data(), snapshots.size(), &captured,
            inputs.data(), inputs.size(), &input_count, FACTORY_FINANCE, &flags));
        EXPECT_EQ(captured, 1u); Write(&state, 0x48, province_begin); Write(&state, 0x4c, province_end); Write(&state, 0x50, province_end);
        factory_node.data[0x31] = std::byte{1};
        EXPECT_FALSE(CollectCountryFactories(Country(country.data()), snapshots.data(), snapshots.size(), &captured,
            inputs.data(), inputs.size(), &input_count, FACTORY_INPUTS, &flags, 48)); EXPECT_NE(flags & FACTORY_UNREADABLE, 0u);
        factory_node.data[0x31] = std::byte{0}; requested_values[0] = 1;
        EXPECT_FALSE(CollectCountryFactories(Country(country.data()), snapshots.data(), snapshots.size(), &captured,
            inputs.data(), inputs.size(), &input_count, FACTORY_INPUTS, &flags, 48)); EXPECT_NE(flags & FACTORY_REQUESTED_INPUT_SENTINEL_INVALID, 0u);
        requested_values[0] = 0; factory_node.data[0x89] = std::byte{1};
        EXPECT_FALSE(CollectCountryFactories(Country(country.data()), snapshots.data(), snapshots.size(), &captured,
            inputs.data(), inputs.size(), &input_count, FACTORY_INPUTS, &flags, 48)); EXPECT_NE(flags & FACTORY_REQUESTED_INPUT_INDEX_INVALID, 0u);
        factory_node.data[0x89] = std::byte{0xff};
        EXPECT_FALSE(CollectCountryFactories(Country(country.data()), snapshots.data(), snapshots.size(), &captured,
            inputs.data(), inputs.size(), &input_count, FACTORY_INPUTS, &flags, 48)); EXPECT_NE(flags & FACTORY_REQUESTED_INPUT_INDEX_INVALID, 0u);
        factory_node.data[0x89] = std::byte{2};
        EXPECT_FALSE(CollectCountryFactories(Country(country.data()), snapshots.data(), snapshots.size(), &captured,
            inputs.data(), inputs.size(), &input_count, FACTORY_INPUTS, &flags, 65)); EXPECT_NE(flags & FACTORY_GOODS_REGISTRY_INVALID, 0u);
        factory_node.next = &factory_node;
        const int malformed_factory_count = 2; Write(&state, 0x68, malformed_factory_count);
        EXPECT_FALSE(CollectCountryFactories(Country(country.data()), snapshots.data(), snapshots.size(), &captured,
            inputs.data(), inputs.size(), &input_count, FACTORY_IDENTITY, &flags)); EXPECT_NE(flags & FACTORY_LIST_INVALID, 0u);
        factory_node.next = nullptr; Write(&state, 0x68, factory_count);
        EXPECT_FALSE(CollectCountryFactories(Country(country.data()), snapshots.data(), 0, &captured,
            inputs.data(), inputs.size(), &input_count, FACTORY_IDENTITY, &flags)); EXPECT_NE(flags & FACTORY_LIMIT, 0u);
    }

    TEST(GameStateReadersTest, CollectsAndValidatesWorldMarketPools)
    {
        std::array<std::byte, 0xd08> game_state{};
        std::array<std::byte, 0x54c> world_market{};
        std::array<std::array<int64_t, 2>, 9> values{};
        constexpr std::array<size_t, 9> offsets{0x08, 0x60, 0x120, 0x178, 0x1d0, 0x280, 0x2d8, 0x434, 0x4f4};
        const void *world_market_pointer = world_market.data();
        Write(&game_state, 0xbcc, world_market_pointer);
        for (size_t index = 0; index < offsets.size(); ++index) {
            values[index] = {0, static_cast<int64_t>((index + 1) * 100)};
            world_market[offsets[index] + 0x08] = std::byte{1};
            const void *begin = values[index].data(); const void *end = values[index].data() + values[index].size();
            Write(&world_market, offsets[index] + 0x48, begin); Write(&world_market, offsets[index] + 0x4c, end); Write(&world_market, offsets[index] + 0x50, end);
        }
        std::array<WorldMarketSnapshot, 4> snapshots{}; uint32_t captured = 0;
        ASSERT_TRUE(CollectWorldMarket(GameState(game_state.data()), snapshots.data(), snapshots.size(), &captured)); ASSERT_EQ(captured, 1u);
        EXPECT_EQ(snapshots[0].good_ordinal, 0); EXPECT_EQ(snapshots[0].supply_raw, 100); EXPECT_EQ(snapshots[0].last_supply_raw, 200);
        EXPECT_EQ(snapshots[0].worldmarket_stock_raw, 300); EXPECT_EQ(snapshots[0].demand_raw, 400); EXPECT_EQ(snapshots[0].real_demand_raw, 500);
        EXPECT_EQ(snapshots[0].price_raw, 600); EXPECT_EQ(snapshots[0].last_price_raw, 700); EXPECT_EQ(snapshots[0].actual_sold_raw, 800); EXPECT_EQ(snapshots[0].actual_sold_world_raw, 900);
        world_market[offsets[5] + 0x09] = std::byte{1}; EXPECT_FALSE(CollectWorldMarket(GameState(game_state.data()), snapshots.data(), snapshots.size(), &captured));
        std::array<std::array<int64_t, 65>, 9> dense_values{};
        for (size_t pool = 0; pool < offsets.size(); ++pool) {
            for (size_t ordinal = 0; ordinal < 64; ++ordinal) { dense_values[pool][ordinal + 1] = static_cast<int64_t>(ordinal + 1); world_market[offsets[pool] + 0x08 + ordinal] = static_cast<std::byte>(ordinal + 1); }
            const void *begin = dense_values[pool].data(); const void *end = dense_values[pool].data() + dense_values[pool].size();
            Write(&world_market, offsets[pool] + 0x48, begin); Write(&world_market, offsets[pool] + 0x4c, end); Write(&world_market, offsets[pool] + 0x50, end);
        }
        std::array<WorldMarketSnapshot, 64> dense_snapshots{};
        ASSERT_TRUE(CollectWorldMarket(GameState(game_state.data()), dense_snapshots.data(), dense_snapshots.size(), &captured));
        EXPECT_EQ(captured, 64u); EXPECT_EQ(dense_snapshots.back().good_ordinal, 63);
    }

    TEST(GameStateReadersTest, ReadsValidatedPopDetailCandidates)
    {
        std::array<std::byte, 0x280> pop{}; std::array<std::byte, 0x60> province{}; std::array<std::byte, 0x2c> pop_type{};
        std::array<std::byte, 0x98> culture{}; std::array<std::byte, 0x74> religion{};
        const int32_t pop_id = 12345, type_id = 4, province_id = 549, size = 4275, employed = 1000;
        const int64_t money = 123456, savings = 789, consciousness = 98337, militancy = 32768, literacy = 22938;
        const int64_t life_needs = 32768, everyday_needs = 24672, luxury_needs = 30, interest = 31, cash_flow = -12;
        const void *province_pointer = province.data(); const void *type_pointer = pop_type.data();
        const void *culture_pointer = culture.data(); const void *religion_pointer = religion.data();
        Write(&province, 0x58, province_id); Write(&pop_type, 0x28, type_id); WriteInlineString(&pop_type, 0x08, "clergymen");
        Write(&pop, 0x0c, pop_id); Write(&pop, 0x58, size); Write(&pop, 0x60, employed); Write(&pop, 0x64, province_pointer);
        Write(&pop, 0x68, type_pointer); Write(&pop, 0x6c, culture_pointer); Write(&pop, 0x70, religion_pointer);
        WriteInlineString(&culture, 0x18, "polish"); WriteInlineString(&religion, 0x10, "catholic");
        Write(&pop, 0x118, consciousness); Write(&pop, 0x120, militancy); Write(&pop, 0x128, literacy);
        Write(&pop, 0x130, life_needs); Write(&pop, 0x138, everyday_needs); Write(&pop, 0x140, luxury_needs);
        Write(&pop, 0x180, money); Write(&pop, 0x210, interest); Write(&pop, 0x218, cash_flow); Write(&pop, 0x250, savings);
        PopDetailSnapshot detail{}; ASSERT_TRUE(ReadPopDetailSnapshot(Pop(pop.data()), &detail));
        EXPECT_EQ(detail.pop_id, pop_id); EXPECT_EQ(detail.pop_type_id_candidate, type_id); EXPECT_EQ(detail.province_id_candidate, province_id);
        EXPECT_EQ(detail.size_candidate, size); EXPECT_EQ(detail.employed_candidate, employed); EXPECT_EQ(detail.consciousness_candidate_raw, consciousness);
        EXPECT_EQ(detail.militancy_candidate_raw, militancy); EXPECT_EQ(detail.literacy_candidate_raw, literacy);
        EXPECT_EQ(detail.economy.money_raw, money); EXPECT_EQ(detail.economy.savings_raw, savings);
        EXPECT_EQ(detail.economy.interest_cash_flow_raw, interest); EXPECT_EQ(detail.economy.total_cash_flow_raw, cash_flow);
        PopIdentityDimensions identity{}; ASSERT_TRUE(ReadPopIdentityDimensions(Pop(pop.data()), &identity));
        EXPECT_STREQ(identity.pop_type_tag_candidate, "clergymen"); EXPECT_STREQ(identity.culture_tag_candidate, "polish"); EXPECT_STREQ(identity.religion_tag_candidate, "catholic");
        Write(&pop, 0x60, size + 1); EXPECT_FALSE(ReadPopDetailSnapshot(Pop(pop.data()), &detail));
        Write(&pop, 0x60, employed); Write(&pop, 0x138, int64_t{32769}); EXPECT_TRUE(ReadPopDetailSnapshot(Pop(pop.data()), &detail));
        PopNeedsSnapshot needs{}; EXPECT_FALSE(ReadPopNeedsSnapshot(Pop(pop.data()), &needs));
        Write(&pop, 0x138, everyday_needs); ASSERT_TRUE(ReadPopNeedsSnapshot(Pop(pop.data()), &needs));
        EXPECT_EQ(needs.life_satisfaction_candidate_raw, life_needs); EXPECT_EQ(needs.everyday_satisfaction_candidate_raw, everyday_needs); EXPECT_EQ(needs.luxury_satisfaction_candidate_raw, luxury_needs);
    }

    TEST(GameStateReadersTest, ReadsValidatedArtisanProductionAndInputs)
    {
        std::array<std::byte, 0x280> pop{}; std::array<std::byte, 0x30> pop_type{}; std::array<std::byte, 0x100> economy{};
        std::array<std::byte, 0x90> production_type{}; std::array<std::byte, 0x30> output_good{};
        std::array<int64_t, 2> stockpile_values{0, 65536}; std::array<int64_t, 2> need_values{0, 98304};
        const void *pop_type_pointer = pop_type.data(); const void *economy_pointer = economy.data();
        const void *production_type_pointer = production_type.data(); const void *output_good_pointer = output_good.data();
        const int32_t pop_id = 8845, output_ordinal = 1; const int64_t base_output = 98304, current_producing = 3116;
        const char artisan_key[] = "artisans"; std::memcpy(pop_type.data() + 0x08, artisan_key, sizeof(artisan_key));
        Write(&pop_type, 0x18, static_cast<uint32_t>(sizeof(artisan_key) - 1)); Write(&pop_type, 0x1c, uint32_t{15});
        const char production_key[] = "artisan_ammunition"; const void *production_key_pointer = production_key;
        Write(&production_type, 0x08, production_key_pointer); Write(&production_type, 0x18, static_cast<uint32_t>(sizeof(production_key) - 1)); Write(&production_type, 0x1c, uint32_t{63});
        const char output_key[] = "ammunition"; std::memcpy(output_good.data() + 0x0c, output_key, sizeof(output_key));
        Write(&output_good, 0x1c, static_cast<uint32_t>(sizeof(output_key) - 1)); Write(&output_good, 0x20, uint32_t{15}); Write(&output_good, 0x08, output_ordinal);
        Write(&production_type, 0x80, output_good_pointer); Write(&production_type, 0x88, base_output);
        Write(&pop, 0x0c, pop_id); Write(&pop, 0x68, pop_type_pointer); Write(&pop, 0x1d4, economy_pointer);
        Write(&economy, 0xb0, production_type_pointer); Write(&economy, 0xb8, int64_t{1126244000}); Write(&economy, 0xc0, current_producing);
        Write(&economy, 0xc8, int64_t{32768}); Write(&economy, 0xd0, int64_t{8496}); Write(&economy, 0xd8, int64_t{0});
        Write(&economy, 0xe0, int64_t{0}); Write(&economy, 0xe8, int64_t{32768}); Write(&economy, 0xf0, int64_t{1126244000}); Write(&economy, 0xf8, int64_t{163852000});
        economy[0x08 + output_ordinal] = std::byte{1}; economy[0x58 + 0x08 + output_ordinal] = std::byte{1};
        const void *stock_begin = stockpile_values.data(); const void *stock_end = stockpile_values.data() + stockpile_values.size();
        const void *need_begin = need_values.data(); const void *need_end = need_values.data() + need_values.size();
        Write(&economy, 0x48, stock_begin); Write(&economy, 0x4c, stock_end); Write(&economy, 0x50, stock_end);
        Write(&economy, 0x58 + 0x48, need_begin); Write(&economy, 0x58 + 0x4c, need_end); Write(&economy, 0x58 + 0x50, need_end);
        ArtisanSnapshot snapshot{}; std::array<ArtisanInputSnapshot, 4> inputs{}; uint32_t input_count = 0;
        ASSERT_TRUE(ReadArtisanSnapshot(Pop(pop.data()), &snapshot, inputs.data(), inputs.size(), &input_count));
        EXPECT_EQ(snapshot.pop_id, pop_id); EXPECT_STREQ(snapshot.production_type, production_key); EXPECT_STREQ(snapshot.output_good, output_key);
        EXPECT_EQ(snapshot.output_good_ordinal, output_ordinal); EXPECT_EQ(snapshot.current_producing_raw, current_producing); EXPECT_EQ(snapshot.gross_output_raw, 9348);
        ASSERT_EQ(input_count, 1u); EXPECT_EQ(inputs[0].good_ordinal, output_ordinal); EXPECT_EQ(inputs[0].stockpile_raw, 65536); EXPECT_EQ(inputs[0].need_raw, 98304);
        Write(&economy, 0xc8, int64_t{32769}); ArtisanReadFailure failure{};
        EXPECT_FALSE(ReadArtisanSnapshot(Pop(pop.data()), &snapshot, inputs.data(), inputs.size(), &input_count, ARTISAN_ALL, &failure));
        EXPECT_EQ(failure.reason, ArtisanReadFailureReason::PercentAfforded); EXPECT_EQ(failure.pop_id, pop_id); EXPECT_EQ(failure.offending_raw, 32769);
        EXPECT_STREQ(ArtisanReadFailureName(failure.reason), "percent_afforded");
        Write(&economy, 0xc8, int64_t{32768}); Write(&economy, 0xd8, int64_t{118028}); EXPECT_TRUE(ReadArtisanSnapshot(Pop(pop.data()), &snapshot, inputs.data(), inputs.size(), &input_count));
    }

    TEST(GameStateReadersTest, RecognizesArtisanWithoutActiveProductionType)
    {
        std::array<std::byte, 0x280> pop{}; std::array<std::byte, 0x30> pop_type{}; std::array<std::byte, 0x100> economy{};
        const void *pop_type_pointer = pop_type.data(); const void *economy_pointer = economy.data(); const char artisan_key[] = "artisans";
        std::memcpy(pop_type.data() + 0x08, artisan_key, sizeof(artisan_key)); Write(&pop_type, 0x18, static_cast<uint32_t>(sizeof(artisan_key) - 1)); Write(&pop_type, 0x1c, uint32_t{15});
        Write(&pop, 0x0c, int32_t{42}); Write(&pop, 0x68, pop_type_pointer); Write(&pop, 0x1d4, economy_pointer);
        int32_t pop_id = -1; EXPECT_TRUE(ReadInactiveArtisan(Pop(pop.data()), &pop_id)); EXPECT_EQ(pop_id, 42);
        const void *active_production = pop_type.data(); Write(&economy, 0xb0, active_production); EXPECT_FALSE(ReadInactiveArtisan(Pop(pop.data()), &pop_id));
    }

    TEST(GameStateReadersTest, ReadsValidatedProvinceRgoRecord)
    {
        std::array<std::byte, 0x1b0> province{}; std::array<std::byte, 0x130> state{}; std::array<std::byte, 0x160> production_type{};
        std::array<std::byte, 0x40> output_good{}; std::array<std::byte, 0x2c> owner_modifier{};
        std::array<int32_t, 2> population_by_type{1000, 848}; std::array<std::array<std::byte, 0xb0>, 2> records{}; std::array<const void *, 3> registry{};
        const void *province_pointer = province.data(); const void *production_type_pointer = production_type.data(); const void *output_good_pointer = output_good.data();
        const void *state_pointer = state.data(); const void *owner_modifier_pointer = owner_modifier.data(); const void *population_begin = population_by_type.data();
        const void *records_begin = records.data(); const void *records_end = records.data() + records.size(); registry = {records_begin, records_end, records_end};
        const char production_key[] = "coal_mine"; const char goods_key[] = "coal";
        const uint32_t production_key_size = sizeof(production_key) - 1, goods_key_size = sizeof(goods_key) - 1, inline_capacity = 15;
        const int32_t goods_ordinal = 10, capacity = 60000, employed = 54730, owner_pop_type_ordinal = 1;
        const int64_t base_output = 78643, base_size = 98304, output_efficiency = 47513, throughput = 17932, income = 536724000;
        const int64_t percent_sold_domestic = 24576, percent_sold_export = 55702, leftover = 32768;
        Write(&province, 0x1ac, capacity); Write(&province, 0x188, state_pointer); Write(&state, 0xc8, capacity); Write(&state, 0x118, population_begin);
        std::memcpy(production_type.data() + 0x08, production_key, sizeof(production_key)); Write(&production_type, 0x18, production_key_size); Write(&production_type, 0x1c, inline_capacity);
        Write(&production_type, 0x80, output_good_pointer); Write(&production_type, 0x88, base_output); Write(&production_type, 0xf0, owner_modifier_pointer); Write(&owner_modifier, 0x28, owner_pop_type_ordinal);
        Write(&output_good, 0x08, goods_ordinal); std::memcpy(output_good.data() + 0x0c, goods_key, sizeof(goods_key)); Write(&output_good, 0x1c, goods_key_size); Write(&output_good, 0x20, inline_capacity);
        Write(&records[1], 0x08, production_type_pointer); Write(&records[1], 0x0c, output_good_pointer); Write(&records[1], 0x1c, province_pointer);
        Write(&records[1], 0x38, output_efficiency); Write(&records[1], 0x40, throughput); Write(&records[1], 0x58, employed); Write(&records[1], 0x80, income); Write(&records[1], 0x88, base_size);
        Write(&records[1], 0x90, percent_sold_domestic); Write(&records[1], 0x98, percent_sold_export); Write(&records[1], 0xa0, leftover);
        RgoSnapshot snapshot{};
        ASSERT_TRUE(ReadProvinceRgo(EmploymentRegistry(registry.data()), Province(province.data()), 1, records.size(), RGO_IDENTITY | RGO_EMPLOYMENT | RGO_PRODUCTION | RGO_FINANCE | RGO_MODIFIERS | RGO_SALES, &snapshot));
        EXPECT_EQ(snapshot.province_id, 1); EXPECT_STREQ(snapshot.production_type, production_key); EXPECT_EQ(snapshot.output_good_ordinal, goods_ordinal); EXPECT_STREQ(snapshot.output_good, goods_key);
        EXPECT_EQ(snapshot.employment_capacity, capacity); EXPECT_EQ(snapshot.employed, employed); EXPECT_EQ(snapshot.base_output_per_size_raw, base_output); EXPECT_EQ(snapshot.base_size_raw, base_size);
        EXPECT_EQ(snapshot.output_efficiency_raw, output_efficiency); EXPECT_EQ(snapshot.throughput_raw, throughput); EXPECT_EQ(snapshot.gross_output_raw, 187206);
        EXPECT_EQ(snapshot.owner_population, 848); EXPECT_EQ(snapshot.state_rgo_employment_capacity, capacity); EXPECT_EQ(snapshot.owner_output_modifier_raw, 463);
        EXPECT_EQ(snapshot.income_raw, income); EXPECT_EQ(snapshot.percent_sold_domestic_raw, percent_sold_domestic); EXPECT_EQ(snapshot.percent_sold_export_raw, percent_sold_export); EXPECT_EQ(snapshot.leftover_raw, leftover);
        const int64_t invalid_percent_sold = 32769; Write(&records[1], 0x90, invalid_percent_sold);
        EXPECT_FALSE(ReadProvinceRgo(EmploymentRegistry(registry.data()), Province(province.data()), 1, records.size(), RGO_SALES, &snapshot)); Write(&records[1], 0x90, percent_sold_domestic);
        const int32_t invalid_owner_pop_type_ordinal = 128; Write(&owner_modifier, 0x28, invalid_owner_pop_type_ordinal);
        EXPECT_FALSE(ReadProvinceRgo(EmploymentRegistry(registry.data()), Province(province.data()), 1, records.size(), RGO_MODIFIERS, &snapshot)); Write(&owner_modifier, 0x28, owner_pop_type_ordinal);
        const void *wrong_province = production_type.data(); Write(&records[1], 0x1c, wrong_province);
        EXPECT_FALSE(ReadProvinceRgo(EmploymentRegistry(registry.data()), Province(province.data()), 1, records.size(), RGO_IDENTITY, &snapshot));
        Write(&records[1], 0x1c, province_pointer); const int64_t maximum = (std::numeric_limits<int64_t>::max)();
        Write(&records[1], 0x38, maximum); Write(&records[1], 0x40, maximum);
        EXPECT_FALSE(ReadProvinceRgo(EmploymentRegistry(registry.data()), Province(province.data()), 1, records.size(), RGO_PRODUCTION, &snapshot));
    }
}
