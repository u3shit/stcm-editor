#ifndef UUID_2DF4B391_2856_41E2_995A_53CABE00D614
#define UUID_2DF4B391_2856_41E2_995A_53CABE00D614
#pragma once

#ifdef NEPTOOLS_WITHOUT_LUA
#define NEPTOOLS_ORDERED_MAP_LUAGEN(name, ...)
#else

#include "ordered_map.hpp"
#include "../lua/function_call.hpp"

namespace Neptools
{

template <typename T, typename Traits,
          typename Compare = std::less<typename Traits::type>>
struct OrderedMapLua
{
    using FakeClass = OrderedMap<T, Traits, Compare>;

    static SmartPtr<T> get(OrderedMap<T, Traits, Compare>& om, size_t i) noexcept
    {
        if (i < om.size()) return &om[i];
        else return nullptr;
    }

    static SmartPtr<T> get(
        OrderedMap<T, Traits, Compare>& om,
        const typename OrderedMap<T, Traits, Compare>::key_type& key)
    {
        auto it = om.find(std::move(key));
        if (it == om.end()) return nullptr;
        else return &*it;
    }

    static void get() noexcept {} // ignore non-int/string keys

    // todo: newindex -- what happens on key collission??
    // todo __ipairs: since lua 5.3, built-in ipairs calls metamethods

    // warning: return values swapped
    static std::tuple<bool, size_t> insert(
        OrderedMap<T, Traits, Compare>& om, size_t i,
        NotNull<SmartPtr<T>>&& t)
    {
        auto r = om.template insert<Check::Throw>(om.nth(i), std::move(t));
        return {r.second, om.index_of(r.first)};
    }

    static size_t erase(OrderedMap<T, Traits, Compare>& om, size_t i, size_t e)
    {
        return om.index_of(om.template erase<Check::Throw>(om.nth(i), om.nth(e)));
    }

    static size_t erase(
        OrderedMap<T, Traits, Compare>& om, size_t i)
    {
        return om.index_of(om.template erase<Check::Throw>(om.nth(i)));
    }

    // lua-compat: returns the erased value
    static NotNull<SmartPtr<T>> remove(
        OrderedMap<T, Traits, Compare>& om, size_t i)
    {
        auto it = om.checked_nth(i);
        NotNull<SmartPtr<T>> ret{&*it};
        om.template erase<Check::Throw>(it);
        return ret;
    }

    // ret: pushed_back, index of old/new item
    static std::tuple<bool, size_t> push_back(
        OrderedMap<T, Traits, Compare>& om,
        NotNull<SmartPtr<T>>&& t)
    {
        auto r = om.template push_back<Check::Throw>(std::move(t));
        return {r.second, om.index_of(r.first)};
    }

    // ret nil if not found
    // ret index, value if found
    static Lua::RetNum find(
        Lua::StateRef vm, OrderedMap<T, Traits, Compare>& om,
        const typename OrderedMap<T, Traits, Compare>::key_type& val)
    {
        auto r = om.find(val);
        if (r == om.end())
        {
            lua_pushnil(vm);
            return 1;
        }
        else
        {
            vm.Push(om.index_of(r));
            vm.Push(*r);
            return 2;
        }
    }

    static Lua::RetNum to_table(
        Lua::StateRef vm, OrderedMap<T, Traits, Compare>& om)
    {
        auto size = om.size();
        lua_createtable(vm, size ? size-1 : size, 0);
        for (size_t i = 0; i < size; ++i)
        {
            vm.Push(om[i]);
            lua_rawseti(vm, -2, i);
        }
        return 1;
    }
};
}

#define NEPTOOLS_ORDERED_MAP_LUAGEN(name, ...)                          \
    template struct ::Neptools::OrderedMapLua<__VA_ARGS__>;             \
    NEPTOOLS_LUA_TEMPLATE(name, (), ::Neptools::OrderedMap<__VA_ARGS__>)

#endif
#endif
