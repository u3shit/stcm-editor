#ifndef UUID_C721F2E1_C293_4D82_8244_2AA0F1B26774
#define UUID_C721F2E1_C293_4D82_8244_2AA0F1B26774
#pragma once

#include "function_call_types.hpp"
#include "type_traits.hpp"

namespace Neptools::Lua
{

namespace Detail
{

template <typename... Args> struct List;

template <typename T> struct FunctionTraits;
template <typename Ret, typename... Args> struct FunctionTraits<Ret(Args...)>
{
    using Return = Ret;
    using Arguments = List<Args...>;
};

template <typename Ret, typename... Args>
struct FunctionTraits<Ret(*)(Args...)> : FunctionTraits<Ret(Args...)> {};
template <typename Ret, typename C, typename... Args>
struct FunctionTraits<Ret(C::*)(Args...)> : FunctionTraits<Ret(C&, Args...)> {};
template <typename Ret, typename C, typename... Args>
struct FunctionTraits<Ret(C::*)(Args...) const> : FunctionTraits<Ret(C&, Args...)> {};

#if __cpp_noexcept_function_type >= 201510
template <typename Ret, typename... Args>
struct FunctionTraits<Ret(*)(Args...) noexcept> : FunctionTraits<Ret(Args...)> {};
template <typename Ret, typename C, typename... Args>
struct FunctionTraits<Ret(C::*)(Args...) noexcept> : FunctionTraits<Ret(C&, Args...)> {};
template <typename Ret, typename C, typename... Args>
struct FunctionTraits<Ret(C::*)(Args...) const noexcept> : FunctionTraits<Ret(C&, Args...)> {};
#endif

template <typename T, int Idx, bool Unsafe> struct GetArg
{
    using Type = typename std::decay<T>::type;
    static constexpr size_t NEXT_IDX = Idx+1;
    static decltype(auto) Get(StateRef vm)
    { return Unsafe ? vm.UnsafeGet<Type>(Idx) : vm.Check<Type>(Idx); }
    static bool Is(StateRef vm) { return vm.Is<Type>(Idx); }
};

template <int Idx, bool Unsafe> struct GetArg<Skip, Idx, Unsafe>
{
    static constexpr size_t NEXT_IDX = Idx+1;
    static Skip Get(StateRef) { return {}; }
    static bool Is(StateRef) { return true; }
};

template <int Idx, bool Unsafe> struct GetArg<StateRef, Idx, Unsafe>
{
    static constexpr size_t NEXT_IDX = Idx;
    static StateRef Get(StateRef vm) { return vm; }
    static bool Is(StateRef) { return true; }
};

template <int Type, int Idx, bool Unsafe> struct GetArg<Raw<Type>, Idx, Unsafe>
{
    static constexpr size_t NEXT_IDX = Idx+1;
    static Raw<Type> Get(StateRef vm)
    {
        if (!Unsafe && !Is(vm))
            vm.TypeError(true, lua_typename(vm, Type), Idx);
        return {};
    }
    static bool Is(StateRef vm) noexcept { return lua_type(vm, Idx) == Type; }
};

template <bool Unsafe, int N, typename Seq, typename... Args>
struct GenArgSequence;
template <bool Unsafe, int N, int... Seq, typename Head, typename... Args>
struct GenArgSequence<Unsafe, N, std::integer_sequence<int, Seq...>, Head, Args...>
{
    using Type = typename GenArgSequence<
        Unsafe,
        GetArg<Head, N, Unsafe>::NEXT_IDX,
        std::integer_sequence<int, Seq..., N>,
        Args...>::Type;
};
template <bool Unsafe, int N, typename Seq> struct GenArgSequence<Unsafe, N, Seq>
{ using Type = Seq; };


template <typename T> struct ResultPush
{
    template <typename U>
    static int Push(StateRef vm, U&& t)
    {
        vm.Push<T>(std::forward<U>(t));
        return 1;
    }
};

template<> struct ResultPush<RetNum>
{ static int Push(StateRef, RetNum r) { return r.n; } };

template <typename Tuple, typename Index> struct TuplePush;
template <typename... Types, size_t... I>
struct TuplePush<std::tuple<Types...>, std::index_sequence<I...>>
{
    static int Push(StateRef vm, const std::tuple<Types...>& ret)
    {
        (vm.Push(std::get<I>(ret)), ...);
        return sizeof...(Types);
    }
};

template<typename... Args> struct ResultPush<std::tuple<Args...>>
    : TuplePush<std::tuple<Args...>,
                std::make_index_sequence<sizeof...(Args)>> {};

// workaround gcc can't mangle noexcept template arguments...
template <typename... Args>
struct NothrowInvokable : std::integral_constant<
    bool, noexcept(Invoke(std::declval<Args>()...))> {};

template <typename... Args>
BOOST_FORCEINLINE
auto CatchInvoke(StateRef, Args&&... args) -> typename std::enable_if<
    NothrowInvokable<Args&&...>::value,
    decltype(Invoke(std::forward<Args>(args)...))>::type
{ return Invoke(std::forward<Args>(args)...); }

inline void ToLuaException(StateRef vm)
{
    auto s = ExceptionToString();
    lua_pushlstring(vm, s.data(), s.size());
    lua_error(vm);
}

template <typename... Args>
auto CatchInvoke(StateRef vm, Args&&... args) -> typename std::enable_if<
    !NothrowInvokable<Args&&...>::value,
    decltype(Invoke(std::forward<Args>(args)...))>::type
{
    try { return Invoke(std::forward<Args>(args)...); }
    catch (const std::exception& e)
    {
        ToLuaException(vm);
        NEPTOOLS_UNREACHABLE("lua_error returned");
    }
}

template <typename T, T Fun, bool Unsafe, typename Ret, typename Args, typename Seq>
struct WrapFunGen;

template <typename T, T Fun, bool Unsafe, typename Ret, typename... Args, int... Seq>
struct WrapFunGen<T, Fun, Unsafe, Ret, List<Args...>, std::integer_sequence<int, Seq...>>
{
    static int Func(lua_State* l)
    {
        StateRef vm{l};
        return ResultPush<Ret>::Push(
            vm, CatchInvoke(vm, Fun, GetArg<Args, Seq, Unsafe>::Get(vm)...));
    }
};

template <typename T, T Fun, bool Unsafe, typename... Args, int... Seq>
struct WrapFunGen<T, Fun, Unsafe, void, List<Args...>, std::integer_sequence<int, Seq...>>
{
    static int Func(lua_State* l)
    {
        StateRef vm{l};
        CatchInvoke(vm, Fun, GetArg<Args, Seq, Unsafe>::Get(vm)...);
        return 0;
    }
};

template <typename T, T Fun, bool Unsafe, typename Args> struct WrapFunGen2;
template <typename T, T Fun, bool Unsafe, typename... Args>
struct WrapFunGen2<T, Fun, Unsafe, List<Args...>>
    : public WrapFunGen<
        T, Fun, Unsafe,
        typename FunctionTraits<T>::Return, List<Args...>,
        typename GenArgSequence<Unsafe, 1, std::integer_sequence<int>, Args...>::Type>
{};

template <typename T, T Fun, bool Unsafe>
struct WrapFunc : WrapFunGen2<T, Fun, Unsafe, typename FunctionTraits<T>::Arguments>
{};

// allow plain old lua functions
template <int (*Fun)(lua_State*), bool Unsafe>
struct WrapFunc<int (*)(lua_State*), Fun, Unsafe>
{ static constexpr const auto Func = Fun; };


// overload
template <typename Args, typename Seq> struct OverloadCheck2;
template <typename... Args, int... Seq>
struct OverloadCheck2<List<Args...>, std::integer_sequence<int, Seq...>>
{
    static bool Is(StateRef vm)
    {
        return (GetArg<Args, Seq, true>::Is(vm) && ...);
    }
};

template <typename Args> struct OverloadCheck;
template <typename... Args>
struct OverloadCheck<List<Args...>>
    : public OverloadCheck2<
        List<Args...>,
        typename GenArgSequence<true, 1, std::integer_sequence<int>, Args...>::Type>
{};

template <typename... Args> struct OverloadWrap;
template <typename T, T Fun, typename... Rest>
struct OverloadWrap<Overload<T, Fun>, Rest...>
{
    static int Func(lua_State* l)
    {
        StateRef vm{l};
        if (OverloadCheck<typename FunctionTraits<T>::Arguments>::Is(vm))
            return WrapFunc<T, Fun, true>::Func(vm);
        else
            return OverloadWrap<Rest...>::Func(vm);
    }
};

template<> struct OverloadWrap<>
{
    static int Func(lua_State* l)
    {
        return luaL_error(l, "Invalid arguments to overloaded function");
    }
};

}

template <typename T, T Fun>
inline void StateRef::Push()
{ lua_pushcfunction(vm, (Detail::WrapFunc<T, Fun, false>::Func)); }

template <typename Head, typename... Tail>
inline typename std::enable_if<IsOverload<Head>::value>::type StateRef::Push()
{ lua_pushcfunction(vm, (Detail::OverloadWrap<Head, Tail...>::Func)); }

}

#endif