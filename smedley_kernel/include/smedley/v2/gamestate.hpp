#pragma once

#include <cstdint>
#include "ai.hpp"
#include "country.hpp"
#include "eu3idler.hpp"
#include "goods.hpp"
#include "history.hpp"
#include "province.hpp"
#include "rebel.hpp"
#include "settings.hpp"
#include "war.hpp"
#include "worldmarket.hpp"
#include "../clausewitz/persistent.hpp"
#include "../clausewitz/types.hpp"
#include "../std/vector.hpp"

namespace smedley::v2
{

    class CPlayer
    {
    };

    /**
     * Container for global diplomacy data. Stores diplomatic
     * history and relations.
     */
    class CDiplomacy : public clausewitz::CPersistent
    {
    protected:
        clausewitz::CList<void *> _relations; // 8
        CDiplomaticHistory _history; // 18
    };

    static_assert(sizeof(CDiplomacy) == 0x38);

    /**
     * CGameState is the container of most globally scoped game data. It
     * represents a snapshot in time of a game session. In its serialized form
     * it is presented as a Victoria 2 save file.
     */
    class CGameState : clausewitz::CPersistent
    {
    protected:
        sstd::vector<int> _canals; // 8
        clausewitz::CList<CPlayer> _players; // 18
        uint8_t _uk_0x28[0x6c];
        clausewitz::CList<int> _outliner; // 94
        sstd::vector<CCountryAI *> _country_ais; // a4
        uint8_t _uk_0xb4[0x9c8];
        CDiplomacy _diplomacy; // a7c
        uint8_t _uk_0xab4[0xc];
        void *_combat_list; // ac0
        uint32_t _uk_0xac4;
        uint32_t _uk_0xac8;
        sstd::vector<CProvince *> _provinces; // acc
        sstd::vector<CCountry *> _countries; // adc
        sstd::vector<int> _player_nations; // aec
        sstd::vector<void *> _unk_0xafc;
        CEU3Date _current_date; // b0c
        CEU3Date _start_date; // b10
        uint32_t _uk_0xb14;
        uint32_t _uk_0xb18;
        uint32_t _uk_0xb1c;
        uint32_t _uk_0xb20;
        CInGameIdler *_idler; // b24
        uint32_t _speed_index; // b28
        uint32_t _uk_0xb2c;
        uint32_t _uk_0xb30;
        uint32_t _uk_0xb34;
        uint32_t _uk_0xb38;
        clausewitz::CList<CWar *> _ongoing_wars; // b3c
        uint32_t _uk_0xb4c;
        uint32_t _uk_0xb50;
        uint32_t _uk_0xb54;
        uint32_t _uk_0xb58;
        CCountryTag _player_tag; // b5c
        uint32_t _uk_0xb64;
        uint32_t _uk_0xb68;
        uint32_t _uk_0xb6c;
        uint32_t _uk_0xb70;
        sstd::vector<void *> _uk_0xb74;
        int _uk_0xb84;
        uint32_t _uk_0xb88;
        uint32_t _uk_0xb8c;
        uint32_t _uk_0xb90;
        bool _great_wars_enabled; // b94
        bool _world_wars_enabled; // b95
        sstd::vector<clausewitz::CList<CRebelFaction *>> _rebel_factions; // b98
        uint32_t _uk_0xba8;
        CGamePlaySettings _gameplay_settings; // bac
        uint32_t _uk_0xbb8;
        uint32_t _uk_0xbbc;
        uint32_t _uk_0xbc0;
        uint32_t _uk_0xbc4;
        uint32_t _uk_0xbc8;
        CWorldMarket *_world_market; // bcc
        sstd::vector<CCountry *> _country_vec; // 0xBD0; TODO: verify how this differs from _countries and whether it is sorted.
        sstd::vector<CCountry *> _great_powers; // be0
        uint32_t _uk_0xbf0;
        uint32_t _uk_0xbf4;
        uint32_t _uk_0xbf8;
        uint32_t _uk_0xbfc;
        sstd::vector<int> _discovered_inventions; // c00
        uint32_t _uk_0xc10;
        CGoodsPool _overseas_penalty; // c14
        CGoodsPool _unit_cost; // c6c
        uint32_t _uk_0xcc4;
        uint32_t _uk_0xcc8;
        uint32_t _uk_0xccc;
        uint32_t _uk_0xcd0;
        clausewitz::CList<int> _pop_growth; // cd4
        uint32_t _uk_0xce4;
        CCountryTag _pop_growth_tag; // ce8
        CEU3Date _pop_growth_date; // cf0
        void *_news_collector; // cf4
        void *_crisis_manager; // cf8
        uint32_t _uk_0xcfc;
        uint32_t _uk_0xd00;
        uint32_t _uk_0xd04;
    public:
        virtual ~CGameState();
        virtual int num_provinces() const; // 18
        virtual int num_countries() const; // 1c
    };

    static_assert(sizeof(CGameState) == 0xd08);

    class CCurrentGameState : public CGameState
    {
    public:
        inline const sstd::vector<CCountry *> countries() const { return _countries; }
        inline sstd::vector<CCountry *> countries() { return _countries; }
        int current_date_raw() const { return _current_date.raw_value(); }
        CInGameIdler *idler() const { return _idler; }
        int speed_index() const { return _speed_index; }
        const CCountryTag &player_tag() const { return _player_tag; }
        CCountry *country(int ordinal) const
        {
            return ordinal >= 0 && static_cast<size_t>(ordinal) < _countries.size()
                ? _countries[ordinal]
                : nullptr;
        }
        CProvince *province(int id) const
        {
            return id >= 0 && static_cast<size_t>(id) < _provinces.size()
                ? _provinces[id]
                : nullptr;
        }
        int player_control_state(int ordinal) const
        {
            return ordinal >= 0 && static_cast<size_t>(ordinal) < _player_nations.size()
                ? _player_nations[ordinal]
                : -1;
        }
        bool has_human_controlled_country() const
        {
            for (size_t ordinal = 0; ordinal < _player_nations.size(); ++ordinal) {
                if (_player_nations[ordinal] != 0) {
                    return true;
                }
            }
            return false;
        }
        size_t country_ai_count() const { return _country_ais.size(); }
        size_t country_count() const { return _countries.size(); }
        bool is_scheduled_ai(const CCountryAI *ai) const
        {
            for (size_t index = 0; index < _country_ais.size(); ++index) {
                if (_country_ais[index] == ai) {
                    return true;
                }
            }
            return false;
        }

        void ReturnCountryToAI(const CCountryTag &tag)
        {
            using ReturnCountryToAIFn = void (__thiscall *)(CCurrentGameState *, uint32_t, int);
            const auto fn = reinterpret_cast<ReturnCountryToAIFn>(memory::Map::base_addr + 0x287a70);
            fn(this, tag.key(), tag.ordinal());
        }

        /*[[[cog
        from codegen import print_class_model_fns
        print_class_model_fns('./models/v2/classes/CCurrentGameState.toml')
        ]]]*/
        static CCurrentGameState * instance()
        {
        const uintptr_t _addr = memory::Map::base_addr + 0xe588e8;
        return *(reinterpret_cast<CCurrentGameState **>(_addr));
        }
        // [[[end]]]

        /** Must be called from the game UI thread, matching the native callers. */
        bool LoadSave(const sstd::string &filename)
        {
            using LoadSaveFn = bool (__stdcall *)(CCurrentGameState *, const sstd::string *);
            const auto fn = reinterpret_cast<LoadSaveFn>(memory::Map::base_addr + 0x27f1d0);
            return fn(this, &filename);
        }
    };

}
