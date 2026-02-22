// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <concepts>

namespace Utils {

template < typename LHS, typename... RHS>
requires (std::equality_comparable_with<LHS, RHS> && ...)
constexpr bool isOneOf(const LHS& lhs, const RHS&... rhs) {
	static_assert(sizeof...(rhs) > 0, "Utils::isOneOf(lhs, rhs...) requires at least one comparison value");
	return ((lhs == rhs) || ...);
}

} // Utils