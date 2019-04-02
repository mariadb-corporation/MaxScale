/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#ifndef SS_DEBUG
#define SS_DEBUG
#endif

#include <iostream>
#include <maxbase/log.hh>
#include <maxscale/config2.hh>

using namespace std;

inline ostream& operator << (ostream& out, const std::chrono::seconds& x)
{
    out << x.count();
    return out;
}

inline ostream& operator << (ostream& out, const std::chrono::milliseconds& x)
{
    out << x.count();
    return out;
}

config::Specification specification("test_module");

config::ParamBool
param_bool(&specification,
           "boolean_parameter",
           "Specifies whether something is enabled.");

config::ParamCount
param_count(&specification,
            "count_parameter",
            "Specifies the cardinality of something.");

config::ParamInteger
param_integer(&specification,
              "integer_parameter",
              "Specifies a number.");

config::ParamDuration<std::chrono::seconds>
param_duration_1(&specification,
                 "duration_parameter_1",
                 "Specifies the duration of something.",
                 mxs::config::INTERPRET_AS_SECONDS);

config::ParamDuration<std::chrono::milliseconds>
param_duration_2(&specification,
                 "duration_parameter_2",
                 "Specifies the duration of something.",
                 mxs::config::INTERPRET_AS_MILLISECONDS);

enum Enum
{
    ENUM_ONE,
    ENUM_TWO
};

config::ParamEnum<Enum>
param_enum(&specification,
           "enum_parameter",
           "Specifies a range of values.",
           {
               { ENUM_ONE, "one" },
               { ENUM_TWO, "two" }
           });

config::ParamPath
param_path(&specification,
           "path_parameter",
           "Specifies the path of something.",
           config::ParamPath::F);

config::ParamSize
param_size(&specification,
           "size_parameter",
           "Specifies the size of something.");

config::ParamString
param_string(&specification,
             "string_parameter",
             "Specifies the name of something.");

template<class T>
struct TestEntry
{
    const char* zText;
    bool        valid;
    T           value;
};

#define elements_in_array(x) (sizeof(x)/sizeof(x[0]))

template<class T>
int test(T& value, const TestEntry<typename T::value_type>* pEntries, int nEntries)
{
    const config::Param& param = value.parameter();

    cout << "Testing " << param.type() << " parameter " << param.name() << "." << endl;

    int nErrors = 0;

    for (int i = 0; i < nEntries; ++i)
    {
        const auto& entry = pEntries[i];

        std::string message;
        bool validated = param.validate(entry.zText, &message);

        if (entry.valid && validated)
        {
            param.set(value, entry.zText);

            if (value != entry.value)
            {
                cout << value.to_string() << " != " << entry.value << endl;
                ++nErrors;
            }
        }
        else if (entry.valid && !validated)
        {
            cout << "Expected \"" << entry.zText << "\" to BE valid for " << param.type()
                 << " parameter " << param.name() << ", but it was NOT validated: " << message << endl;
            ++nErrors;
        }
        else if (!entry.valid && validated)
        {
            cout << "Expected \"" << entry.zText << "\" NOT to be valid for " << param.type()
                 << " parameter " << param.name() << ", but it WAS validated." << endl;
            ++nErrors;
        }
    }

    return nErrors;
}

int test_bool(config::Bool& value)
{
    static const TestEntry<config::Bool::value_type> entries[] =
    {
        { "1",     true, true },
        { "0",     true, false },
        { "true",  true, true },
        { "false", true, false },
        { "on",    true, true },
        { "off",   true, false },

        { "2",     false },
        { "truth", false },
        { "%&",    false },
        { "-1",    false },
    };

    return test(value, entries, elements_in_array(entries));
}

int test_count(config::Count& value)
{
    static const TestEntry<config::Count::value_type> entries[] =
    {
        { "1",    true, 1 },
        { "9999", true, 9999 },
        { "0",    true, 0 },

        { "0x45", false },
        { "blah", false },
        { "-1",   false },
    };

    return test(value, entries, elements_in_array(entries));
}

int test_duration(config::Duration<std::chrono::seconds>& value)
{
    static const TestEntry<config::Duration<std::chrono::seconds>::value_type> entries[] =
    {
        { "1",      true, std::chrono::seconds { 1 } },
        { "1ms",    true, std::chrono::seconds { 0 } },
        { "1s",     true, std::chrono::seconds { 1 } },
        { "1m",     true, std::chrono::seconds { 60 } },
        { "1h",     true, std::chrono::seconds { 3600 } },

        { "1x",     false },
        { "a",      false },
        { "-",      false },
        { "second", false }
    };

    return test(value, entries, elements_in_array(entries));
}

int test_duration(config::Duration<std::chrono::milliseconds>& value)
{
    static const TestEntry<config::Duration<std::chrono::milliseconds>::value_type> entries[] =
    {
        { "1",      true, std::chrono::milliseconds { 1 } },
        { "1ms",    true, std::chrono::milliseconds { 1 } },
        { "1s",     true, std::chrono::milliseconds { 1000 } },
        { "1m",     true, std::chrono::milliseconds { 60000 } },
        { "1h",     true, std::chrono::milliseconds { 3600000 } },

        { "1x",     false },
        { "a",      false },
        { "-",      false },
        { "second", false }
    };

    return test(value, entries, elements_in_array(entries));
}

int test_enum(config::Enum<Enum>& value)
{
    static const TestEntry<Enum> entries[] =
    {
        { "one",  true, ENUM_ONE },
        { "two",  true, ENUM_TWO },

        { "blah", false },
        { "1",    false },
        { "ones", false }
    };

    return test(value, entries, elements_in_array(entries));
}

int test_path(config::Path& value)
{
    static const TestEntry<config::Path::value_type> entries[] =
    {
        { ".",    true, "." },
        { "/tmp", true, "/tmp" },

        { "non-existent", false }
    };

    return test(value, entries, elements_in_array(entries));
}

int test_size(config::Size& value)
{
    static const TestEntry<config::Size::value_type> entries[] =
    {
        { "0",   true, 0 },
        { "100", true, 100 },

        { "-100",  false },
        { "0x100", false },
    };

    return test(value, entries, elements_in_array(entries));
}

int test_string(config::String& value)
{
    static const TestEntry<config::String::value_type> entries[] =
    {
        { "blah",     true, "blah" },
        { "\"blah\"", true, "blah" },
        { "'blah'",   true, "blah" },
        { "123",      true, "123" },
        { "`blah`",   true, "`blah`" },

        { "'blah\"",  false }
    };

    return test(value, entries, elements_in_array(entries));
}

int test_integer(config::Integer& value)
{
    static const TestEntry<config::Integer::value_type> entries[] =
    {
        { "0",           true, 0 },
        { "-1",          true, -1 },
        { "1",           true, 1 },
        { "-2147483648", true, -2147483648 },
        { "2147483647",  true, 2147483647 },

        { "-2147483649", false },
        { "2147483648",  false },
        { "0x10"  ,      false },
    };

    return test(value, entries, elements_in_array(entries));
}

int main()
{
    mxb::Log log;

    for_each(specification.cbegin(), specification.cend(),[](const config::Specification::value_type& p) {
            cout << p.second->documentation() << endl;
        });

    cout << endl;

    specification.document(cout);

    config::Configuration configuration(&specification);

    int nErrors = 0;

    config::Bool value_bool(&configuration, &param_bool);
    nErrors += test_bool(value_bool);

    config::Count value_count(&configuration, &param_count);
    nErrors += test_count(value_count);

    config::Duration<std::chrono::seconds> value_duration_1(&configuration, &param_duration_1);
    nErrors += test_duration(value_duration_1);

    config::Duration<std::chrono::milliseconds> value_duration_2(&configuration, &param_duration_2);
    nErrors += test_duration(value_duration_2);

    config::Enum<Enum> value_enum(&configuration, &param_enum);
    nErrors += test_enum(value_enum);

    config::Path value_path(&configuration, &param_path);
    nErrors += test_path(value_path);

    config::Size value_size(&configuration, &param_size);
    nErrors += test_size(value_size);

    config::String value_string(&configuration, &param_string);
    nErrors += test_string(value_string);

    config::Integer value_integer(&configuration, &param_integer);
    nErrors += test_integer(value_integer);

    return nErrors ? EXIT_FAILURE : EXIT_SUCCESS;
}
