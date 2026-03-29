#pragma once

#include "reflection_accessors.hpp"
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <string>
#include <string_view>

namespace nb = nanobind;

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
            s += "=" + nb::cast<std::string>(nb::repr(nb::cast(self.[:m:])));
            first = false;
        }
    }
}

template <typename T>
void bind_reflected_struct(nb::class_<T>& cl) {
    constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));

    template for (constexpr auto m : members) {
        if constexpr (std::meta::has_identifier(m)) {
            cl.def_prop_rw(
                std::meta::identifier_of(m).data(),
                [](const T& self) { return Accessor<T, m>::get(self); },
                [](T& self, typename Accessor<T, m>::FieldType v) { Accessor<T, m>::set(self, v); });
        }
    }

    cl.def("__repr__", [](const T& self) {
        std::string s = std::string(std::meta::identifier_of(^^T).data()) + "(";
        stringify_members(self, s);
        return s + ")";
    });
}

template <typename T>
void bind_reflected_member_functions(nb::class_<T>& cl) {
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
