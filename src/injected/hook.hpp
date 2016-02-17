#ifndef UUID_310CE7F9_73D3_4140_BA43_9DA7CB4136D1
#define UUID_310CE7F9_73D3_4140_BA43_9DA7CB4136D1
#pragma once

#include <cstddef>

extern char* image_base;

void* Hook(void* hook, void* dst, size_t copy);

template <typename T>
inline T Hook(void* hook, T dst, size_t copy)
{
    static_assert(sizeof(T) == 4, "");
    union
    {
        T t;
        void* vd;
    } u;
    u.t = dst;
    u.vd = Hook(hook, u.vd, copy);
    return u.t;
}

template <typename T>
T& As(void* ptr) { return *static_cast<T*>(ptr); }

class Unprotect
{
public:
    Unprotect(void* ptr, size_t len);
    ~Unprotect();
    Unprotect(const Unprotect&) = delete;
    void operator=(const Unprotect&) = delete;

private:
    void* ptr;
    size_t len;
    unsigned long orig_prot;
};

#endif
