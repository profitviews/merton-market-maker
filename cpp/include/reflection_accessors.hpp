#pragma once

#include <meta>

// Named accessor to avoid lambda mangling crashes (reflection / expansion).
template <typename T, std::meta::info M>
struct Accessor {
    using FieldType = [: std::meta::type_of(M) :];
    static FieldType get(const T& self) { return self.[:M:]; }
    static void set(T& self, FieldType v) { self.[:M:] = v; }
};
