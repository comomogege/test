/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "core/stl_subset.h"

namespace base {

template <typename T>
struct custom_is_fast_copy_type : public std_::false_type {
};
// To make your own type a fast copy type just write:
// template <>
// struct base::custom_is_fast_copy_type<MyTinyType> : public std_::true_type {
// };

namespace internal {

template <typename ...Types>
struct type_list_contains;

template <typename T>
struct type_list_contains<T> : public std_::false_type {
};

template <typename T, typename Head, typename ...Types>
struct type_list_contains<T, Head, Types...> : public std_::integral_constant<bool, std_::is_same<Head, T>::value || type_list_contains<T, Types...>::value> {
};

template <typename T>
using is_std_unsigned_int = type_list_contains<T, unsigned char, unsigned short int, unsigned int, unsigned long int>;

template <typename T>
using is_std_signed_int = type_list_contains<T, signed char, short int, int, long int>;

template <typename T>
using is_std_integral = std_::integral_constant<bool, is_std_unsigned_int<T>::value || is_std_signed_int<T>::value || type_list_contains<T, bool, char, wchar_t>::value>;

template <typename T>
using is_std_float = type_list_contains<T, float, double, long double>;

template <typename T>
using is_std_arith = std_::integral_constant<bool, is_std_integral<T>::value || is_std_float<T>::value>;

template <typename T>
using is_std_fundamental = std_::integral_constant<bool, is_std_arith<T>::value || std_::is_same<T, void>::value>;

template <typename T>
struct is_pointer : public std_::false_type {
};

template <typename T>
struct is_pointer<T*> : public std_::true_type {
};

template <typename T>
struct is_member_pointer : public std_::false_type {
};

template <typename T, typename C>
struct is_member_pointer<T C::*> : public std_::true_type {
};

template <typename T>
using is_fast_copy_type = std_::integral_constant<bool, is_std_fundamental<T>::value || is_pointer<T>::value || is_member_pointer<T>::value || custom_is_fast_copy_type<T>::value>;

template <typename T>
struct add_const_reference {
	using type = const T &;
};

template <>
struct add_const_reference<void> {
	using type = void;
};

template <typename T>
using add_const_reference_t = typename add_const_reference<T>::type;

template <typename T>
struct remove_pointer {
	using type = T;
};

template <typename T>
struct remove_pointer<T*> {
	using type = T;
};

template <typename T>
using remove_pointer_t = typename remove_pointer<T>::type;

} // namespace internal

template <typename T>
struct type_traits {
	using is_std_unsigned_int = internal::is_std_unsigned_int<T>;
	using is_std_signed_int = internal::is_std_signed_int<T>;
	using is_std_integral = internal::is_std_integral<T>;
	using is_std_float = internal::is_std_float<T>;
	using is_std_arith = internal::is_std_arith<T>;
	using is_std_fundamental = internal::is_std_fundamental<T>;
	using is_pointer = internal::is_pointer<T>;
	using is_member_pointer = internal::is_member_pointer<T>;
	using is_fast_copy_type = internal::is_fast_copy_type<T>;

	using parameter_type = std_::conditional_t<is_fast_copy_type::value, T, internal::add_const_reference_t<T>>;
	using pointed_type = internal::remove_pointer_t<T>;
};

} // namespace base
