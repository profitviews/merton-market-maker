#pragma once

#include <meta>
#include <pybind11/pybind11.h>
#include <string>
#include <string_view>

namespace py = pybind11;

// Named Accessor to avoid lambda mangling crashes.
template <typename T, std::meta::info M>
struct Accessor {
    using FieldType = [: std::meta::type_of(M) :];
    static FieldType get(T& self) { return self.[:M:]; }
    static void set(T& self, FieldType v) { self.[:M:] = v; }
};

// Isolated stringifier to avoid expansion-statement bugs inside lambdas.
template <typename T>
void stringify_members(const T& self, std::string& s) {
    constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));

    bool first = true;
    template for (constexpr auto m : members) {
        if constexpr (std::meta::has_identifier(m)) {
            if (!first) {
                s += ", ";
            }
            s += std::meta::identifier_of(m).data();
            s += "=" + py::repr(py::cast(self.[:m:])).template cast<std::string>();
            first = false;
        }
    }
}

// Global binder for any reflected struct.
template <typename T>
void bind_reflected_struct(py::class_<T>& cl) {
    constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));

    template for (constexpr auto m : members) {
        if constexpr (std::meta::has_identifier(m)) {
            cl.def_property(std::meta::identifier_of(m).data(), &Accessor<T, m>::get, &Accessor<T, m>::set);
        }
    }

    cl.def("__repr__", [](const T& self) {
        std::string s = std::string(std::meta::identifier_of(^^T).data()) + "(";
        stringify_members(self, s);
        return s + ")";
    });
}

template <typename T>
void bind_reflected_member_functions(py::class_<T>& cl) {
    constexpr auto members = std::define_static_array(
        std::meta::members_of(^^T, std::meta::access_context::current()));

    template for (constexpr auto m : members) {
        if constexpr (
            std::meta::is_public(m) &&
            std::meta::is_function(m) &&
            !std::meta::is_constructor(m) &&
            !std::meta::is_destructor(m) &&
            !std::meta::is_conversion_function(m) &&
            !std::meta::is_operator_function(m)
        ) {
            if constexpr (std::meta::has_identifier(m)) {
                constexpr auto fnName = std::meta::identifier_of(m);
                if constexpr (std::meta::is_static_member(m)) {
                    cl.def_static(fnName.data(), &[:m:]);
                } else {
                    cl.def(fnName.data(), &[:m:]);
                }
            }
        }
    }
}

