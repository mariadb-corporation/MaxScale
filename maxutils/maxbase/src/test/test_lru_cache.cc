/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/ccdefs.hh>
#include <maxbase/lru_cache.hh>
#include <iostream>
#include <sstream>

#define EXPECT(a) \
        if (!(a)) { \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " + #a); \
        }

template<class KvContainer>
std::string to_string(const KvContainer& c)
{
    std::ostringstream ss;

    for (const auto& [k, v] : c)
    {
        ss << k << "=" << v << ",";
    }

    return ss.str();
}

template<class Container>
bool equal(const Container& c, std::string expected, size_t n)
{
    bool ok = true;
    size_t s = c.size();

    if (s != n)
    {
        std::cerr << "Expected " << n << " elements but found " << s << std::endl;
        ok = false;
    }

    if (auto result = to_string(c); result != expected)
    {
        std::cerr << "Expected '" << expected << "' got '" << result << "'" << std::endl;
        ok = false;
    }
    else
    {
        std::cout << result << std::endl;
    }

    return ok;
}

void test_int_lru()
{
    mxb::lru_cache<int, int> l;
    EXPECT(l.empty());
    EXPECT(l.size() == 0);
    EXPECT(l.find(0) == l.end());

    l.emplace(1, 1);
    EXPECT(equal(l, "1=1,", 1));

    l.emplace(2, 2);
    EXPECT(equal(l, "2=2,1=1,", 2));

    l.insert(std::make_pair(3, 3));
    EXPECT(equal(l, "3=3,2=2,1=1,", 3));

    EXPECT(l.find(0) == l.end());
    EXPECT(equal(l, "3=3,2=2,1=1,", 3));

    EXPECT(l.find(1) != l.end());
    EXPECT(equal(l, "1=1,3=3,2=2,", 3));

    EXPECT(l.peek(2) != l.end());
    EXPECT(equal(l, "1=1,3=3,2=2,", 3));

    EXPECT(l.find(2) != l.end());
    EXPECT(equal(l, "2=2,1=1,3=3,", 3));

    EXPECT(l.find(1) != l.end());
    EXPECT(equal(l, "1=1,2=2,3=3,", 3));

    EXPECT(l.find(3) != l.end());
    EXPECT(equal(l, "3=3,1=1,2=2,", 3));

    l.emplace(4, 4);
    EXPECT(equal(l, "4=4,3=3,1=1,2=2,", 4));

    l.pop_back();
    EXPECT(equal(l, "4=4,3=3,1=1,", 3));

    EXPECT(l.find(0) == l.end());
    EXPECT(equal(l, "4=4,3=3,1=1,", 3));

    EXPECT(l.find(1) != l.end());
    EXPECT(equal(l, "1=1,4=4,3=3,", 3));

    l.pop_front();
    EXPECT(equal(l, "4=4,3=3,", 2));

    l.emplace(5, 5);
    EXPECT(equal(l, "5=5,4=4,3=3,", 3));

    l.erase(4);
    EXPECT(equal(l, "5=5,3=3,", 2));

    EXPECT(l.find(3) != l.end());
    EXPECT(equal(l, "3=3,5=5,", 2));

    l.clear();
    EXPECT(equal(l, "", 0));
}

struct A
{
    std::string value;
};

std::ostream& operator<<(std::ostream& os, const A& a)
{
    os << a.value;
    return os;
}

void test_string_view_lru()
{
    mxb::lru_cache<std::string_view, A> l;
    A v1{"!"};
    A v2{"world"};
    A v3{"hello"};
    std::string_view k1 = v1.value;
    std::string_view k2 = v2.value;
    std::string_view k3 = v3.value;

    l.emplace(k1, v1);
    l.emplace(k2, v2);
    l.emplace(k3, v3);
    EXPECT(equal(l, "hello=hello,world=world,!=!,", 3));
    EXPECT(l.front().first == k3);
    EXPECT(l.back().first == k1);

    l.emplace(k2, v2);
    EXPECT(l.find(k2) != l.end());
    EXPECT(equal(l, "world=world,hello=hello,!=!,", 3));

    l.find("!");
    EXPECT(equal(l, "!=!,world=world,hello=hello,", 3));

    l.erase("hello");
    EXPECT(equal(l, "!=!,world=world,", 2));

    l.find("world");
    EXPECT(equal(l, "world=world,!=!,", 2));
}

int main(int argc, char** argv)
{
    try
    {
        test_int_lru();
        test_string_view_lru();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
