/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <algorithm>
#include <functional>
#include <numeric>

/**
 * Helper functions for calculating statistics over STL containers of classes. Containers of
 * fundamental types aren't supported as the standard library functions already implement it.
 */

namespace maxscale
{

template<typename T>
using ValueType = typename T::value_type;

/**
 * Calculate sum of members
 *
 * @param values Container of values
 * @param member Member of T::value_type to use
 *
 * @return Sum of member values
 */
template<typename T, typename R>
R sum(const T& values, R ValueType<T>::* member)
{
    return std::accumulate(values.begin(),
                           values.end(),
                           R {},
                           [&](R r, ValueType<T> t) {
                               return r + t.*member;
                           });
}

/**
 * Calculate average of members
 *
 * @param values Container of values
 * @param member Member of T::value_type to use
 *
 * @return Average of member values
 */
template<typename T, typename R>
R avg(const T& values, R ValueType<T>::* member)
{
    return values.empty() ? R {} : sum(values, member) / static_cast<R>(values.size());
}

/**
 * Get minimum member value
 *
 * @param values Container of values
 * @param member Member of T::value_type to use
 *
 * @return The minimum value of T::*member in `values`
 */
template<typename T, typename R>
R min(const T& values, R ValueType<T>::* member)
{
    auto it = std::min_element(values.begin(),
                               values.end(),
                               [&](ValueType<T> a, ValueType<T> b) {
                                   return a.*member < b.*member;
                               });
    return it != values.end() ? (*it).*member : R {};
}

/**
 * Get maximum member value
 *
 * @param values Container of values
 * @param member Member of T::value_type to use
 *
 * @return The maximum value of T::*member in `values`
 */
template<typename T, typename R>
R max(const T& values, R ValueType<T>::* member)
{
    auto it = std::max_element(values.begin(),
                               values.end(),
                               [&](ValueType<T> a, ValueType<T> b) {
                                   return a.*member < b.*member;
                               });
    return it != values.end() ? (*it).*member : R {};
}

/**
 * Helper function for accumulating container-like member values
 *
 * This function accumulates the values element-wise with `accum` and returns the resulting container.
 *
 * @param values Container of values
 * @param member Member of T::value_type to use
 * @param accum  Accumulator function
 *
 * @return Accumulated container
 */
template<typename T, typename R, typename Accum>
R accumulate(const T& values, R ValueType<T>::* member, Accum accum)
{
    return std::accumulate(values.begin(),
                           values.end(),
                           R {},
                           [&](R r, const ValueType<T>& t) {

                               std::transform(r.begin(),
                                              r.end(),
                                              (t.*member).begin(),
                                              r.begin(),
                                              [&](ValueType<R> a, ValueType<R> b) {
                                                  return accum(a, b);
                                              });

                               return r;
                           });
}

/**
 * Calculate sum of member container values
 *
 * @param values Container of values
 * @param member Member of T::value_type to use
 *
 * @return Sum of members
 */
template<typename T, typename R>
R sum_element(const T& values, R ValueType<T>::* member)
{
    return accumulate(values, member, std::plus<ValueType<R>>());
}

/**
 * Calculate average of member container values
 *
 * @param values Container of values
 * @param member Member of T::value_type to use
 *
 * @return Average of members
 */
template<typename T, typename R>
R avg_element(const T& values, R ValueType<T>::* member)
{
    auto result = sum_element(values, member);

    for (auto&& a : result)
    {
        // Using C-style cast to work around an uncrustify bug
        a /= (ValueType<R>)(values.size());
    }

    return result;
}

/**
 * Calculate minimum of member container values
 *
 * @param values Container of values
 * @param member Member of T::value_type to use
 *
 * @return Minimum of members
 */
template<typename T, typename R>
R min_element(const T& values, R ValueType<T>::* member)
{
    return accumulate(values,
                      member,
                      [](const ValueType<R>& a, const ValueType<R>& b) {
                          return std::min(a, b);
                      });
}

/**
 * Calculate maximum of member container values
 *
 * @param values Container of values
 * @param member Member of T::value_type to use
 *
 * @return Maximum of members
 */
template<typename T, typename R>
R max_element(const T& values, R ValueType<T>::* member)
{
    return accumulate(values,
                      member,
                      [](const ValueType<R>& a, const ValueType<R>& b) {
                          return std::max(a, b);
                      });
}
}
