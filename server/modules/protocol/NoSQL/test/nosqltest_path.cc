/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosql.hh"

using namespace nosql;
using namespace std;

struct Test
{
    struct Incarnation
    {
        string path;
        string parent_path;
        string array_path;
    };

    string              key;
    vector<Incarnation> incarnations;
};

ostream& operator << (ostream& out, const vector<Path::Incarnation>& incarnations)
{
    auto it = incarnations.begin();
    while (it != incarnations.end())
    {
        if (it != incarnations.begin())
        {
            out << ", ";
        }

        out << "(\"" << it->path() << "\", \"" << it->parent_path() << "\", \"" << it->array_path() << "\")";
        ++it;
    }

    return out;
}

ostream& operator << (ostream& out, const vector<Test::Incarnation>& incarnations)
{
    auto it = incarnations.begin();
    while (it != incarnations.end())
    {
        if (it != incarnations.begin())
        {
            out << ", ";
        }

        out << "(\"" << it->path << "\", \"" << it->parent_path << "\", \"" << it->array_path << "\")";
        ++it;
    }

    return out;
}

namespace std
{

template<>
struct less<Path::Incarnation>
{
    bool operator()(const Path::Incarnation& lhs, const Path::Incarnation& rhs) const
    {
        return lhs.path() < rhs.path();
    }
};

template<>
struct less<Test::Incarnation>
{
    bool operator()(const Test::Incarnation& lhs, const Test::Incarnation& rhs) const
    {
        return lhs.path < rhs.path;
    }
};

}

bool operator == (const vector<Path::Incarnation>& in1, const vector<Test::Incarnation>& in2)
{
    bool rv = false;

    set<Path::Incarnation> lhs(in1.begin(), in1.end());
    set<Test::Incarnation> rhs(in2.begin(), in2.end());

    if (lhs.size() == rhs.size())
    {
        rv = std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](const Path::Incarnation& l,
                                                                const Test::Incarnation& r) {
                            return
                            (l.path() == r.path)
                            && (l.parent_path() == r.parent_path)
                            && (l.array_path() == r.array_path);
                        });
    }

    return rv;
}

int test(const Test& t)
{
    int rv = 0;

    cout << t.key << ": " << endl;

    vector<Path::Incarnation> incarnations = Path::get_incarnations(t.key);

    cout << "Paths : ";

    if (incarnations == t.incarnations)
    {
        cout << incarnations << "\n";
    }
    else
    {
        cout << incarnations << " != " << t.incarnations << "\n";
        ++rv;
    }

    cout << endl;

    return rv;
}

int main()
{
    Test tests[] = {
        {
            "a",
            {
                {
                    { "a", "", "" }
                }
            }
        },
        {
            "a.b",
            {
                {
                    { "a.b", "a", "" },
                    { "a[*].b", "a", "a" }
                }
            }
        },
        {
            "a.b.c",
            {
                {
                    { "a.b.c", "a.b", "" },
                    { "a[*].b.c", "a[*].b", "a" },
                    { "a.b[*].c", "a.b", "a.b" },
                    { "a[*].b[*].c", "a[*].b", "a[*].b" },
                }
            }
        },
        {
            "a.1.b",
            {
                {
                    { "a.1.b", "a.1", "" },
                    { "a[1].b", "a[1]", "a" },
                    { "a[*].1.b", "a[*].1", "a" },
                    { "a.1[*].b", "a.1", "a.1" },
                    { "a[*].1[*].b", "a[*].1", "a[*].1" }
                }
            }
        },
        {
            "a.1",
            {
                { "a.1", "a", "" },
                { "a[1]", "a", "a" },
                { "a[*].1", "a", "a" }
            }
        }
    };

    int rv = 0;

    for (const auto& t : tests)
    {
        rv += test(t);
    }

    if (rv != 0)
    {
        cout << "ERROR" << endl;
    }

    return rv;
}
