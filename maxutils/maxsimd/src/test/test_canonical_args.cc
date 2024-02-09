/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/ccdefs.hh>

#include <iostream>

#include <maxbase/string.hh>
#include <maxsimd/canonical.hh>

struct TestCase
{
    std::string              sql;
    std::string              canonical;
    std::vector<std::string> args;

    // Only used by the test case that has a trailing comment in it
    std::string recombined;
};

std::vector<TestCase> tests
{
    {
        R"(select count(*) from t1 where id not in (1,2);)",
        R"(select count(*) from t1 where id not in (?,?);)",
        {"1", "2"}
    },
    {
        R"(select count(*) from t1 where match a against ('000000');)",
        R"(select count(*) from t1 where match a against (?);)",
        {"'000000'"}
    },
    {
        R"(SELECT COUNT(*) FROM t1 WHERE MATCH(a) AGAINST("+awrd bwrd* +cwrd*" IN BOOLEAN MODE);)",
        R"(SELECT COUNT(*) FROM t1 WHERE MATCH(a) AGAINST(? IN BOOLEAN MODE);)",
        {R"("+awrd bwrd* +cwrd*")"}
    },
    {
        R"(select count(*) from t1 where s1 < 0 or s1 is null;)",
        R"(select count(*) from t1 where s1 < ? or s1 is null;)",
        {"0"}
    },
    {
        R"(SELECT COUNT(*) FROM t1 WHERE s1 = 1001;)",
        R"(SELECT COUNT(*) FROM t1 WHERE s1 = ?;)",
        {"1001"}
    },
    {
        R"(select count(*) from t1 where x < -16;)",
        R"(select count(*) from t1 where x < -?;)",
        {"16"}
    },
    {
        R"(select count(*) from t1 where x = 16;)",
        R"(select count(*) from t1 where x = ?;)",
        {"16"}
    },
    {
        R"(select count(*) from t1 where x = 18446744073709551601;)",
        R"(select count(*) from t1 where x = ?;)",
        {"18446744073709551601"}
    },
    {
        R"(select truncate(5678.123451,6);)",
        R"(select truncate(?,?);)",
        {"5678.123451", "6"}
    },
    {
        R"(select truncate(99999999999999999999999999999999999999,-31);)",
        R"(select truncate(?,-?);)",
        {"99999999999999999999999999999999999999", "31"}
    },
    {
        R"(select v/10;)",
        R"(select v/?;)",
        {"10"}
    },
    {
        R"(select uncompress("");)",
        R"(select uncompress(?);)",
        {R"("")"}
    },
    {
        R"(SELECT UNHEX('G');)",
        R"(SELECT UNHEX(?);)",
        {"'G'"}
    },
    {
        R"(select unhex(hex("foobar")), hex(unhex("1234567890ABCDEF")), unhex("345678"), unhex(NULL);)",
        R"(select unhex(hex(?)), hex(unhex(?)), unhex(?), unhex(NULL);)",
        {R"("foobar")", R"("1234567890ABCDEF")", R"("345678")"}
    },
    {
        R"(select UpdateXML('<a>a1<b>b1<c>c1</c>b2</b>a2</a>','/a/b/c','+++++++++');)",
        R"(select UpdateXML(?,?,?);)",
        {"'<a>a1<b>b1<c>c1</c>b2</b>a2</a>'", "'/a/b/c'", "'+++++++++'"}
    },
    {
        R"(select UpdateXML(@xml, '/a/@aa1', '');)",
        R"(select UpdateXML(@xml, ?, ?);)",
        {"'/a/@aa1'", "''"}
    },
    {
        R"(SELECT user, host FROM mysql.user where user = 'CUser' order by 1,2;)",
        R"(SELECT user, host FROM mysql.user where user = ? order by ?,?;)",
        {"'CUser'", "1", "2"}
    },
    {
        R"(select user, host, password, plugin, authentication_string from mysql.user where user = 'u1';)",
        R"(select user, host, password, plugin, authentication_string from mysql.user where user = ?;)",
        {"'u1'"}
    },
    {
        R"(select userid,count(*) from t1 group by userid desc having 3  IN (1,COUNT(*));)",
        R"(select userid,count(*) from t1 group by userid desc having ?  IN (?,COUNT(*));)",
        {"3", "1"}
    },
    {
        R"(select userid,count(*) from t1 group by userid desc having (count(*)+1) IN (4,3);)",
        R"(select userid,count(*) from t1 group by userid desc having (count(*)+?) IN (?,?);)",
        {"1", "4", "3"}
    },
    {
        R"(SELECT user_id FROM t1 WHERE request_id=9999999999999;)",
        R"(SELECT user_id FROM t1 WHERE request_id=?;)",
        {"9999999999999"}
    },
    {
        R"(SELECT UserId FROM t1 WHERE UserId=22 group by Userid;)",
        R"(SELECT UserId FROM t1 WHERE UserId=? group by Userid;)",
        {"22"}
    },
    {
        R"(select yearweek("2000-01-01",0) as '2000', yearweek("2001-01-01",0) as '2001', yearweek("2002-01-01",0) as '2002';)",
        R"(select yearweek(?,?) as ?, yearweek(?,?) as ?, yearweek(?,?) as ?;)",
        {R"("2000-01-01")", "0", "'2000'", R"("2001-01-01")", "0", "'2001'", R"("2002-01-01")", "0", "'2002'"}
    },
    {
        R"(select user() like "%@%";)",
        R"(select user() like ?;)",
        {R"("%@%")"}
    },
    {
        R"(select utext from t1 where utext like '%%';)",
        R"(select utext from t1 where utext like ?;)",
        {"'%%'"}
    },
    {
        R"(SELECT _utf32 0x10001=_utf32 0x10002;)",
        R"(SELECT _utf32 ?=_utf32 ?;)",
        {"0x10001", "0x10002"}
    },
    {
        R"(select _utf32'a' collate utf32_general_ci = 0xfffd;)",
        R"(select _utf32? collate utf32_general_ci = ?;)",
        {"'a'", "0xfffd"}
    },
    {
        R"(SELECT _utf8 0x7E, _utf8 X'7E', _utf8 B'01111110';)",
        R"(SELECT _utf8 ?, _utf8 X?, _utf8 B?;)",
        {"0x7E", "'7E'", "'01111110'"}
    },
    {
        R"(select _utf8 0xD0B0D0B1D0B2 like concat(_utf8'%',_utf8 0xD0B1,_utf8 '%');)",
        R"(select _utf8 ? like concat(_utf8?,_utf8 ?,_utf8 ?);)",
        {"0xD0B0D0B1D0B2", "'%'", "0xD0B1", "'%'"}
    },
    {
        R"(SELECT _utf8mb3'test';)",
        R"(SELECT _utf8mb3?;)",
        {"'test'"}
    },
    {
        R"(select (_utf8 X'616263FF');)",
        R"(select (_utf8 X?);)",
        {"'616263FF'"}
    },
    {
        R"(SELECT v1.f4 FROM v1  WHERE f1<>0 OR f2<>0 AND f4='v' AND (f2<>0 OR f3<>0 AND f5<>0 OR f4 LIKE '%b%');)",
        R"(SELECT v1.f4 FROM v1  WHERE f1<>? OR f2<>? AND f4=? AND (f2<>? OR f3<>? AND f5<>? OR f4 LIKE ?);)",
        {"0", "0", "'v'", "0", "0", "0", "'%b%'"}
    },
    {
        R"(SELECT v2 FROM t1 WHERE v1  IN  ('f', 'd', 'h', 'u' ) AND i  =  2;)",
        R"(SELECT v2 FROM t1 WHERE v1  IN  (?, ?, ?, ? ) AND i  =  ?;)",
        {"'f'", "'d'", "'h'", "'u'", "2"}
    },
    {
        R"(select "-- comment # followed by another comment" as "-- more comments";# this should be removed)",
        R"(select ? as ?;)",
        {R"("-- comment # followed by another comment")", R"("-- more comments")"},
        R"(select "-- comment # followed by another comment" as "-- more comments";)",
    },
    {
        R"(select @ujis4 = CONVERT(@utf84 USING ujis);)",
        R"(select @ujis4 = CONVERT(@utf84 USING ujis);)",
        {}
    },
    {
        R"(SELECT @v5, @v6, @v7, @v8, @v9, @v10;)",
        R"(SELECT @v5, @v6, @v7, @v8, @v9, @v10;)",
        {}
    },
    {
        R"(SELECT a$1, $b5555, c$ from mysqltest.$test1)",
        R"(SELECT a$1, $b5555, c$ from mysqltest.$test1)",
        {}
    },
    {
        R"(SELECT 1ea10.1a20, 1e+ 1e+10 from 1ea10)",
        R"(SELECT 1ea10.1a20, 1e+ ? from 1ea10)",
        {"1e+10"}
    },
    {
        R"(SELECT 0e0, 0.0e-0, -1e+1,  -999.999e999, -00.99e-99, +00.99e+99, +42-42e42, 42E-1-2+3)",
        R"(SELECT ?, ?, -?,  -?, -?, +?, +?-?, ?-?+?)",
        {"0e0", "0.0e-0", "1e+1", "999.999e999", "00.99e-99", "00.99e+99", "42", "42e42", "42E-1", "2", "3"}
    },
    {
        R"(SELECT '''''''''')",
        R"(SELECT ?)",
        {"''''''''''"}
    },
};

std::string to_string(const std::vector<std::string>& strs)
{
    return mxb::join(strs, ", ");
}

std::string to_string(const maxsimd::CanonicalArgs& strs)
{
    return mxb::transform_join(strs,
                               [](const auto& a){
        return a.value;
    }, ", ");
}

bool check(const std::string& canonical, const maxsimd::CanonicalArgs& result, const TestCase& test)
{
    bool ok = result.size() == test.args.size();

    if (ok)
    {
        for (size_t i = 0; i < result.size(); i++)
        {
            if (result[i].value != test.args[i])
            {
                std::cout << "Value mismatch at " << i + 1 << "!" << std::endl;
                std::cout << "Expected: " << to_string(test.args) << std::endl;
                std::cout << "Result:   " << to_string(result) << std::endl;
                ok = false;
                break;
            }
        }
    }
    else
    {
        std::cout << "Size mismatch!" << std::endl;
        std::cout << "Expected: " << test.args.size() << std::endl;
        std::cout << "Result:   " << result.size() << std::endl;
    }

    if (canonical != test.canonical)
    {
        std::cout << "Canonical mismatch!" << std::endl;
        std::cout << "Expected: " << test.canonical << std::endl;
        std::cout << "Result:   " << canonical << std::endl;
        ok = false;
    }

    auto recombined = maxsimd::canonical_args_to_sql(canonical, result);
    auto expected_sql = test.recombined.empty() ? test.sql : test.recombined;

    if (recombined != expected_sql)
    {
        std::cout << "Recombination mismatch!" << std::endl;
        std::cout << "Expected: " << expected_sql << std::endl;
        std::cout << "Result:   " << recombined << std::endl;
    }

    return ok;
}

int main(int argc, char** argv)
{
    int rc = EXIT_SUCCESS;

    for (const auto& test : tests)
    {
        std::string sql = test.sql;
        maxsimd::CanonicalArgs args;
        maxsimd::get_canonical_args(&sql, &args);

        if (!check(sql, args, test))
        {
            std::cout << "Error! SQL: " << test.sql << "\n\n";
            rc = EXIT_FAILURE;
        }
    }

    return rc;
}
