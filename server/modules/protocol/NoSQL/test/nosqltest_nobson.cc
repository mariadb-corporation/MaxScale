/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <cmath>
#include <vector>
#include <bsoncxx/types/bson_value/value.hpp>
#include "nosqlnobson.hh"

using namespace nosql;
using namespace std;
namespace bson_value = bsoncxx::types::bson_value;

using Operation = bsoncxx::types::bson_value::value (*)(const bson_value::view& lhs,
                                                        const bson_value::view& rhs);

struct Test
{
    bson_value::value lhs;
    bson_value::value rhs;
    Operation         op;
    bson_value::value result;
};

int run_tests(const char* zName, const vector<Test>& tests)
{
    int rv = 0;

    int nCase = 0;
    for (const Test& test : tests)
    {
        cout << "Testing: " << nobson::to_json_expression(test.lhs) << " "
             << zName << " " << nobson::to_json_expression(test.rhs)
             << " = " << nobson::to_json_expression(test.result) << flush;

        auto result = test.op(test.lhs, test.rhs);
        auto e = test.result.view(); // Expected result
        auto o = result.view(); // Obtained result

        if (o.type() != e.type() || o != e)
        {
            cerr << ", got ("
                 << bsoncxx::to_string(o.type()) << ")" << nobson::to_json_expression(o)
                 << " instead of expected ("
                 << bsoncxx::to_string(e.type()) << ")" << nobson::to_json_expression(e);
            ++rv;
        }

        cout << "." << endl;

        ++nCase;
    }

    return rv;
}

/**
 * Add
 */
template<typename Type, typename PromotedType>
void create_add_tests(vector<Test>& tests)
{
    // Smoke test.
    tests.push_back(
        {
            bson_value::value(static_cast<Type>(2)),
            bson_value::value(static_cast<Type>(3)),
            &nobson::add,
            bson_value::value(static_cast<Type>(5))
        });

    // Just below the upper edge.
    tests.push_back(
        {
            bson_value::value(std::numeric_limits<Type>::max() - 2),
            bson_value::value(static_cast<Type>(1)),
            &nobson::add,
            bson_value::value(std::numeric_limits<Type>::max() - 1)
        });

    // Just above the upper edge.
    tests.push_back(
        {
            bson_value::value(std::numeric_limits<Type>::max()),
            bson_value::value(static_cast<Type>(1)),
            &nobson::add,
            bson_value::value(static_cast<PromotedType>(std::numeric_limits<Type>::max()) + 1)
        });

    // Just above the lower edge.
    tests.push_back(
        {
            bson_value::value(std::numeric_limits<Type>::min() + 2),
            bson_value::value(static_cast<Type>(-1)),
            &nobson::add,
            bson_value::value(std::numeric_limits<Type>::min() + 1)
        });

    // Just below the lower edge.
    tests.push_back(
        {
            bson_value::value(std::numeric_limits<Type>::min()),
            bson_value::value(static_cast<Type>(-1)),
            &nobson::add,
            bson_value::value(static_cast<PromotedType>(std::numeric_limits<Type>::min()) - 1)
        });
}

int test_add()
{
    vector<Test> tests;

    create_add_tests<int32_t, int64_t>(tests);
    create_add_tests<int64_t, double>(tests);

    return run_tests("+", tests);
}

/**
 * Sub
 */
template<typename Type, typename PromotedType>
void create_sub_tests(vector<Test>& tests)
{
    // Smoke test.
    tests.push_back(
        {
            bson_value::value(static_cast<Type>(5)),
            bson_value::value(static_cast<Type>(2)),
            &nobson::sub,
            bson_value::value(static_cast<Type>(3))
        });

    // Just below the upper edge.
    tests.push_back(
        {
            bson_value::value(std::numeric_limits<Type>::max() - 2),
            bson_value::value(static_cast<Type>(-1)),
            &nobson::sub,
            bson_value::value(std::numeric_limits<Type>::max() - 1)
        });

    // Just above the upper edge.
    tests.push_back(
        {
            bson_value::value(std::numeric_limits<Type>::max()),
            bson_value::value(static_cast<Type>(-1)),
            &nobson::sub,
            bson_value::value(static_cast<PromotedType>(std::numeric_limits<Type>::max()) + 1)
        });

    // Just above the lower edge.
    tests.push_back(
        {
            bson_value::value(std::numeric_limits<Type>::min() + 2),
            bson_value::value(static_cast<Type>(1)),
            &nobson::sub,
            bson_value::value(std::numeric_limits<Type>::min() + 1)
        });

    // Just below the lower edge.
    tests.push_back(
        {
            bson_value::value(std::numeric_limits<Type>::min()),
            bson_value::value(static_cast<Type>(1)),
            &nobson::sub,
            bson_value::value(static_cast<PromotedType>(std::numeric_limits<Type>::min()) - 1)
        });
}

int test_sub()
{
    vector<Test> tests;

    create_sub_tests<int32_t, int64_t>(tests);
    create_sub_tests<int64_t, double>(tests);

    return run_tests("-", tests);
}

/**
 * Mul
 */
template<typename Type, typename PromotedType>
void create_mul_tests(vector<Test>& tests)
{
    // Smoke test.
    tests.push_back(
        {
            bson_value::value(static_cast<Type>(5)),
            bson_value::value(static_cast<Type>(2)),
            &nobson::mul,
            bson_value::value(static_cast<Type>(10))
        });

    // Just below the upper edge.
    {
        Type lhs = sqrt(std::numeric_limits<Type>::max());
        Type rhs = lhs;
        Type result = lhs * rhs;

        tests.push_back(
            {
                bson_value::value(lhs),
                bson_value::value(rhs),
                &nobson::mul,
                bson_value::value(result)
            });
    }

    // Just above the upper edge.
    {
        Type lhs = sqrt(std::numeric_limits<Type>::max()) + 1;
        Type rhs = lhs;
        PromotedType result = static_cast<PromotedType>(lhs) * rhs;

        tests.push_back(
            {
                bson_value::value(lhs),
                bson_value::value(rhs),
                &nobson::mul,
                bson_value::value(result)
            });
    }

    // Just above the lower edge.
    {
        Type lhs = sqrt(std::numeric_limits<Type>::max());
        Type rhs = -lhs;
        Type result = lhs * rhs;

        tests.push_back(
            {
                bson_value::value(lhs),
                bson_value::value(rhs),
                &nobson::mul,
                bson_value::value(result)
            });
    }

    // Just below the lower edge.
    {
        Type lhs = sqrt(std::numeric_limits<Type>::max()) + 1;
        Type rhs = -lhs;
        PromotedType result = static_cast<PromotedType>(lhs) * rhs;

        tests.push_back(
            {
                bson_value::value(lhs),
                bson_value::value(rhs),
                &nobson::mul,
                bson_value::value(result)
            });
    }
}

int test_mul()
{
    vector<Test> tests;

    create_mul_tests<int32_t, int64_t>(tests);
    create_mul_tests<int64_t, double>(tests);

    return run_tests("*", tests);
}

/**
 * Div
 */
template<typename Type, typename PromotedType>
void create_div_tests(vector<Test>& tests)
{
    // Smoke test.
    tests.push_back(
        {
            bson_value::value(static_cast<Type>(10)),
            bson_value::value(static_cast<Type>(2)),
            &nobson::div,
            bson_value::value(static_cast<Type>(5))
        });

    // The one and only tricky case
    tests.push_back(
        {
            bson_value::value(std::numeric_limits<Type>::min()),
            bson_value::value(static_cast<Type>(-1)),
            &nobson::div,
            bson_value::value(static_cast<PromotedType>(std::numeric_limits<Type>::max()) + 1)
        });
}

int test_div()
{
    vector<Test> tests;

    create_div_tests<int32_t, int64_t>(tests);
    create_div_tests<int64_t, double>(tests);

    return run_tests("/", tests);
}



int main()
{
    int rv = 0;

    rv += test_add();
    rv += test_sub();
    rv += test_mul();
    rv += test_div();

    return rv;
}

