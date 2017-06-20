#ifndef UUID_AFD13C49_2B38_4C98_88BE_F8D45F347F14
#define UUID_AFD13C49_2B38_4C98_88BE_F8D45F347F14
#pragma once

#include "base.hpp"
#include "function_call_types.hpp"
#include "../nullable.hpp"
#include "../nonowning_string.hpp"

namespace boost { namespace filesystem { class path; } }

namespace Neptools
{
template <typename T> class NotNull;

namespace Lua
{

// type name
template <typename T, typename Enable = void> struct TypeName
{ static constexpr const char* TYPE_NAME = T::TYPE_NAME; };

template <typename T>
struct TypeName<T, std::enable_if_t<std::is_integral<T>::value>>
{ static constexpr const char* TYPE_NAME = "integer"; };

template <typename T>
struct TypeName<T, std::enable_if_t<std::is_floating_point<T>::value>>
{ static constexpr const char* TYPE_NAME = "number"; };

template<> struct TypeName<bool>
{ static constexpr const char* TYPE_NAME = "boolean"; };

template<> struct TypeName<const char*>
{ static constexpr const char* TYPE_NAME = "string"; };
template<> struct TypeName<std::string>
{ static constexpr const char* TYPE_NAME = "string"; };

template <typename T>
constexpr const char* TYPE_NAME = TypeName<T>::TYPE_NAME;

#define NEPTOOLS_LUA_CLASS         \
    public:                        \
    static const char TYPE_NAME[]

#define NEPTOOLS_ENUM(name)                         \
    template<> struct Neptools::Lua::TypeName<name> \
    { static const char TYPE_NAME[]; }

// type tag
template <typename T>
std::enable_if_t<TypeTraits<T>::TYPE_TAGGED, char>
TYPE_TAG = {};

// lauxlib operations:
// luaL_check*: call lua_to*, fail if it fails
// luaL_opt*: lua_isnoneornil ? default : luaL_check*

template <typename T> struct IsBoostEndian : std::false_type {};

template <typename T>
struct TypeTraits<T, std::enable_if_t<
    std::is_integral<T>::value || std::is_enum<T>::value ||
    IsBoostEndian<T>::value>>
{
    static T Get(StateRef vm, bool arg, int idx)
    {
        int isnum;
        // use tonumber instead of tointeger
        // in luajit/ljx lua_Integer is ptrdiff_t, which means 32 or 64 bits
        // depending on architecture... avoid this compatibility madness
#ifndef LUA_VERSION_LJX
#error "Update code for normal lua"
#endif
        auto ret = lua_tonumberx(vm, idx, &isnum);
        if (BOOST_LIKELY(isnum)) return static_cast<T>(ret);
        vm.TypeError(arg, TYPE_NAME<T>, idx);
    }

    static T UnsafeGet(StateRef vm, bool, int idx)
    { return static_cast<T>(lua_tonumberx(vm, idx, nullptr)); }

    static bool Is(StateRef vm, int idx)
    { return lua_type(vm, idx) == LUA_TNUMBER; }

    static void Push(StateRef vm, T val)
    { lua_pushnumber(vm, lua_Number(val)); }

    static constexpr int LUA_TYPE = LUA_TNUMBER;
    static constexpr bool TYPE_TAGGED = false; // needed for enums
};

template <typename T>
struct TypeTraits<T, std::enable_if_t<std::is_floating_point<T>::value>>
{
    static T Get(StateRef vm, bool arg, int idx)
    {
        int isnum;
        auto ret = lua_tonumberx(vm, idx, &isnum);
        if (BOOST_LIKELY(isnum)) return static_cast<T>(ret);
        vm.TypeError(arg, TYPE_NAME<T>, idx);
    }

    static T UnsafeGet(StateRef vm, bool, int idx)
    { return static_cast<T>(lua_tonumberx(vm, idx, nullptr)); }

    static bool Is(StateRef vm, int idx)
    { return lua_type(vm, idx) == LUA_TNUMBER; }

    static void Push(StateRef vm, T val)
    { lua_pushnumber(vm, val); }

    static constexpr int LUA_TYPE = LUA_TNUMBER;
};

template <>
struct TypeTraits<bool>
{
    static bool Get(StateRef vm, bool arg, int idx)
    {
        if (BOOST_LIKELY(lua_isboolean(vm, idx)))
            return lua_toboolean(vm, idx);
        vm.TypeError(arg, TYPE_NAME<bool>, idx);
    }

    static bool UnsafeGet(StateRef vm, bool, int idx)
    { return lua_toboolean(vm, idx); }

    static bool Is(StateRef vm, int idx)
    { return lua_isboolean(vm, idx); }

    static void Push(StateRef vm, bool val)
    { lua_pushboolean(vm, val); }

    static constexpr int LUA_TYPE = LUA_TBOOLEAN;
};

template<>
struct TypeTraits<const char*>
{
    static const char* Get(StateRef vm, bool arg, int idx)
    {
        auto str = lua_tostring(vm, idx);
        if (BOOST_LIKELY(!!str)) return str;
        vm.TypeError(arg, TYPE_NAME<const char*>, idx);
    };

    static const char* UnsafeGet(StateRef vm, bool, int idx)
    { return lua_tostring(vm, idx); }

    static bool Is(StateRef vm, int idx)
    { return lua_type(vm, idx) == LUA_TSTRING; }

    static void Push(StateRef vm, const char* val)
    { lua_pushstring(vm, val); }

    static constexpr int LUA_TYPE = LUA_TSTRING;
};

template <typename T>
struct TypeTraits<T, std::enable_if_t<
    std::is_same<T, std::string>::value ||
    std::is_same<T, NonowningString>::value ||
    std::is_same<T, StringView>::value>>
{
    static T Get(StateRef vm, bool arg, int idx)
    {
        size_t len;
        auto str = lua_tolstring(vm, idx, &len);
        if (BOOST_LIKELY(!!str)) return T(str, len);
        vm.TypeError(arg, TYPE_NAME<std::string>, idx);
    };

    static T UnsafeGet(StateRef vm, bool, int idx)
    {
        size_t len;
        auto str = lua_tolstring(vm, idx, &len);
        return T(str, len);
    }

    static bool Is(StateRef vm, int idx)
    { return lua_type(vm, idx) == LUA_TSTRING; }

    static void Push(StateRef vm, const T& val)
    { lua_pushlstring(vm, val.data(), val.length()); }

    static constexpr int LUA_TYPE = LUA_TSTRING;
};

template<>
struct TypeTraits<boost::filesystem::path> : public TypeTraits<const char*>
{
    template <typename T> // T will be boost::filesystem::path, but it's only
                          // fwd declared at the moment...
    static void Push(StateRef vm, const T& pth)
    {
#ifdef WINDOWS
        auto str = pth.string();
        lua_pushlstring(vm, str.c_str(), str.size());
#else
        lua_pushlstring(vm, pth.c_str(), pth.size());
#endif
    }
};

template <typename T, typename Ret = T>
struct NullableTypeTraits
{
    using NotNullable = std::remove_reference_t<typename ToNotNullable<T>::Type>;

    static Ret Get(StateRef vm, bool arg, int idx)
    {
        if (lua_isnil(vm, idx)) return nullptr;
        return ToNullable<NotNullable>::Conv(
            TypeTraits<NotNullable>::Get(vm, arg, idx));
    }

    static Ret UnsafeGet(StateRef vm, bool arg, int idx)
    {
        if (lua_isnil(vm, idx)) return nullptr;
        return ToNullable<NotNullable>::Conv(
            TypeTraits<NotNullable>::UnsafeGet(vm, arg, idx));
    }

    static bool Is(StateRef vm, int idx)
    { return lua_isnil(vm, idx) || TypeTraits<NotNullable>::Is(vm, idx); }

    static void Push(StateRef vm, const T& obj)
    {
        if (obj) TypeTraits<NotNullable>::Push(vm, ToNotNullable<T>::Conv(obj));
        else lua_pushnil(vm);
    }

    using RawType = typename TypeTraits<NotNullable>::RawType;
};

template <typename T>
struct TypeTraits<T*> : NullableTypeTraits<T*> {};

// used by UserType
template <typename T, typename Enable = void> struct UserTypeTraits;

template <typename T>
struct UserTypeTraits<T, std::enable_if_t<std::is_enum_v<T>>>
{
    inline static void MetatableCreate(StateRef) {}
    static constexpr bool NEEDS_GC = false;
};

template <typename T, typename Enable = void>
struct HasLuaType : std::false_type {};
template <typename T>
struct HasLuaType<T, std::void_t<decltype(TypeTraits<T>::LUA_TYPE)>>
    : std::true_type {};

template <typename T, typename Enable = void> struct RawType
{ using Type = typename TypeTraits<T>::RawType; };
template <typename T>
struct RawType<T, std::void_t<decltype(TypeTraits<T>::LUA_TYPE)>>
{ using Type = Raw<TypeTraits<T>::LUA_TYPE>; };

template <int T> struct RawType<Raw<T>> { using Type = Raw<T>; };
template <> struct RawType<Skip> { using Type = Skip; };

template <typename T>
using RawTypeT = typename RawType<T>::Type;


// todo: nullable/optional Bs
template <typename A, typename B>
constexpr const bool COMPATIBLE_WITH =
    std::is_same_v<A, Skip> || std::is_base_of_v<RawTypeT<A>, RawTypeT<B>>;

static_assert(!COMPATIBLE_WITH<Raw<LUA_TTABLE>, Raw<LUA_TSTRING>>);
static_assert(COMPATIBLE_WITH<int, double>);

}
}

#endif
