#pragma once

#include <cstdint>

namespace smedley::game_state
{
    template <typename Category>
    class Reference;

    namespace detail
    {
        struct CountryReferenceCategory;
        struct StateReferenceCategory;
        struct ProvinceReferenceCategory;
        struct PopReferenceCategory;
        struct FactoryReferenceCategory;
        struct GameStateReferenceCategory;
        struct EmploymentRegistryReferenceCategory;

        template <typename Category>
        const void *RawPointer(const Reference<Category> &reference) noexcept;
    }

    template <typename Category>
    class Reference
    {
    public:
        Reference() = default;
        // Construction labels an address; checked readers still establish accessibility and invariants.
        explicit Reference(const void *pointer) noexcept : pointer_(pointer) {}
        template <typename T>
        explicit Reference(T *pointer) = delete;

        explicit operator bool() const noexcept { return pointer_ != nullptr; }
        uintptr_t address() const noexcept { return reinterpret_cast<uintptr_t>(pointer_); }

    private:
        template <typename OtherCategory>
        friend const void *detail::RawPointer(const Reference<OtherCategory> &reference) noexcept;

        const void *pointer_ = nullptr;
    };

    namespace detail
    {
        template <typename Category>
        const void *RawPointer(const Reference<Category> &reference) noexcept
        {
            return reference.pointer_;
        }
    }

    using CountryRef = Reference<detail::CountryReferenceCategory>;
    using StateRef = Reference<detail::StateReferenceCategory>;
    using ProvinceRef = Reference<detail::ProvinceReferenceCategory>;
    using PopRef = Reference<detail::PopReferenceCategory>;
    using FactoryRef = Reference<detail::FactoryReferenceCategory>;
    using GameStateRef = Reference<detail::GameStateReferenceCategory>;
    using EmploymentRegistryRef = Reference<detail::EmploymentRegistryReferenceCategory>;
}
