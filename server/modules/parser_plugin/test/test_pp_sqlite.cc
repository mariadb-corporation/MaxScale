#include <iostream>
#include <stdexcept>
#include <sstream>

#include <maxbase/assert.hh>
#include <maxscale/parser.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

using namespace std::literals::string_literals;

int errors = 0;

#define expect(a, fmt, ...)                    \
    do{if (!(a)) {                             \
           const char* what = #a;              \
           printf("Error: %s\n", what);        \
           printf(fmt, ##__VA_ARGS__);         \
           printf("\n");                       \
           ++errors;                           \
       }}while(false)

class Tester
{
public:
    Tester(const char* zParser_plugin, const mxs::Parser::Helper* pHelper)
    {
        mxs::set_datadir("/tmp");
        mxs::set_langdir(".");
        mxs::set_process_datadir("/tmp");

        if (!mxs_log_init(NULL, ".", MXB_LOG_TARGET_DEFAULT))
        {
            throw std::runtime_error("Failed to initialize the log");
        }

        m_pPlugin = load_plugin(zParser_plugin);

        if (!m_pPlugin)
        {
            throw std::runtime_error("Failed to load "s + zParser_plugin);
        }

        m_sParser = m_pPlugin->create_parser(pHelper);

        uint64_t version = 10 * 1000 * 3 * 100;
        m_sParser->set_server_version(version);
    }

    ~Tester()
    {
        if (m_pPlugin)
        {
            mxs::ParserPlugin::unload(m_pPlugin);
        }
    }

    mxs::sql::OpCode get_operation(const std::string& sql)
    {
        GWBUF buffer = mariadb::create_query(sql);

        return m_sParser->get_operation(buffer);
    }

    uint32_t get_type(const std::string& sql)
    {
        GWBUF buffer = mariadb::create_query(sql);

        return m_sParser->get_type_mask(buffer);
    }

    mxs::Parser::KillInfo get_kill(const std::string& sql)
    {
        GWBUF buffer = mariadb::create_query(sql);

        return m_sParser->get_kill_info(buffer);
    }

private:

    mxs::ParserPlugin* load_plugin(const char* name)
    {
        std::string libdir = "../"s + name;
        mxs::set_libdir(libdir.c_str());
        mxs::ParserPlugin* pPlugin = mxs::ParserPlugin::load(name);

        if (pPlugin)
        {
            setenv("PP_ARGS", "log_unrecognized_statements=1", 1);

            if (!pPlugin->setup(mxs::Parser::SqlMode::DEFAULT) || !pPlugin->thread_init())
            {
                std::cerr << "error: Could not setup or init plugin " << name << "." << std::endl;
                mxs::ParserPlugin::unload(pPlugin);
                pPlugin = nullptr;
            }
        }
        else
        {
            std::cerr << "error: Could not load plugin " << name << "." << std::endl;
        }

        return pPlugin;
    }

    mxs::ParserPlugin*           m_pPlugin = nullptr;
    std::unique_ptr<mxs::Parser> m_sParser;
};

static std::vector<std::tuple<std::string, uint32_t, mxs::sql::OpCode>> test_cases
{
    {
        "select sleep(2);",
        mxs::sql::TYPE_READ,
        mxs::sql::OP_SELECT
    },
    {
        "select * from tst where lname like '%e%' order by fname;",
        mxs::sql::TYPE_READ,
        mxs::sql::OP_SELECT
    },
    {
        "insert into tst values ('Jane','Doe'),('Daisy','Duck'),('Marie','Curie');",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_INSERT
    },
    {
        "update tst set fname='Farmer', lname='McDonald' where lname='%Doe' and fname='John';",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_UPDATE
    },
    {
        "create table tmp as select * from t1;",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_CREATE_TABLE
    },
    {
        "create temporary table tmp as select * from t1;",
        mxs::sql::TYPE_WRITE | mxs::sql::TYPE_CREATE_TMP_TABLE,
        mxs::sql::OP_CREATE_TABLE
    },
    {
        "select @@server_id;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_SYSVAR_READ,
        mxs::sql::OP_SELECT
    },
    {
        "select @OLD_SQL_NOTES;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_USERVAR_READ,
        mxs::sql::OP_SELECT
    },
    {
        "SET autocommit=1;",
        mxs::sql::TYPE_SESSION_WRITE | mxs::sql::TYPE_GSYSVAR_WRITE | mxs::sql::TYPE_ENABLE_AUTOCOMMIT
        | mxs::sql::TYPE_COMMIT,
        mxs::sql::OP_SET
    },
    {
        "SET autocommit=0;",
        mxs::sql::TYPE_SESSION_WRITE | mxs::sql::TYPE_GSYSVAR_WRITE | mxs::sql::TYPE_BEGIN_TRX
        | mxs::sql::TYPE_DISABLE_AUTOCOMMIT,
        mxs::sql::OP_SET
    },
    {
        "BEGIN;",
        mxs::sql::TYPE_BEGIN_TRX,
        mxs::sql::OP_UNDEFINED
    },
    {
        "ROLLBACK;",
        mxs::sql::TYPE_ROLLBACK,
        mxs::sql::OP_UNDEFINED
    },
    {
        "COMMIT;",
        mxs::sql::TYPE_COMMIT,
        mxs::sql::OP_UNDEFINED
    },
    {
        "use X;",
        mxs::sql::TYPE_SESSION_WRITE,
        mxs::sql::OP_CHANGE_DB
    },
    {
        "select last_insert_id();",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_MASTER_READ,
        mxs::sql::OP_SELECT
    },
    {
        "select @@last_insert_id;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_MASTER_READ,
        mxs::sql::OP_SELECT
    },
    {
        "select @@identity;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_MASTER_READ,
        mxs::sql::OP_SELECT
    },
    {
        "select if(@@hostname='box02','prod_mariadb02','n');",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_SYSVAR_READ,
        mxs::sql::OP_SELECT
    },
    {
        "select next value for seq1;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "select nextval(seq1);",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "select seq1.nextval;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT GET_LOCK('lock1',10);",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT IS_FREE_LOCK('lock1');",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT IS_USED_LOCK('lock1');",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT RELEASE_LOCK('lock1');",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "deallocate prepare select_stmt;",
        mxs::sql::TYPE_DEALLOC_PREPARE,
        mxs::sql::OP_UNDEFINED
    },
    {
        "SELECT a FROM tbl FOR UPDATE;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT a INTO OUTFILE 'out.txt';",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT a INTO DUMPFILE 'dump.txt';",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT a INTO @var;",
        mxs::sql::TYPE_GSYSVAR_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "select timediff(cast('2004-12-30 12:00:00' as time), '12:00:00');",
        mxs::sql::TYPE_READ,
        mxs::sql::OP_SELECT
    },
    {
        "(select 1 as a from t1) union all (select 1 from dual) limit 1;",
        mxs::sql::TYPE_READ,
        mxs::sql::OP_SELECT
    },
    {
        "SET @saved_cs_client= @@character_set_client;",
        mxs::sql::TYPE_SESSION_WRITE | mxs::sql::TYPE_USERVAR_WRITE,
        mxs::sql::OP_SET
    },
    {
        "SELECT 1 AS c1 FROM t1 ORDER BY ( SELECT 1 AS c2 FROM t1 GROUP BY GREATEST(LAST_INSERT_ID(), t1.a) ORDER BY GREATEST(LAST_INSERT_ID(), t1.a) LIMIT 1);",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_MASTER_READ,
        mxs::sql::OP_SELECT
    },
    {
        "SET PASSWORD FOR 'user'@'10.0.0.1'='*C50EB75D7CB4C76B5264218B92BC69E6815B057A';",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SET
    },
    {
        "SELECT UTC_TIMESTAMP();",
        mxs::sql::TYPE_READ,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT COUNT(IF(!c.ispackage, 1, NULL)) as cnt FROM test FOR UPDATE;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT handler FROM abc FOR UPDATE;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT * FROM test LOCK IN SHARE MODE;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "SELECT * FROM test FOR SHARE;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_SELECT
    },
    {
        "DELETE x FROM x JOIN (SELECT id FROM y) y ON x.id = y.id;",
        mxs::sql::TYPE_READ | mxs::sql::TYPE_WRITE,
        mxs::sql::OP_DELETE
    },

    // MXS-3377: Parsing of KILL queries
    {
        "KILL 1",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL USER 'bob'",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL CONNECTION 2",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL CONNECTION USER 'bob'",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL QUERY 3",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL QUERY USER 'bob'",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL QUERY ID 4",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL HARD 5",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL HARD USER 'bob'",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL HARD CONNECTION 6",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL HARD CONNECTION USER 'bob'",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL HARD QUERY 7",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL HARD QUERY USER 'bob'",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL HARD QUERY ID 8",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL SOFT 9",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL SOFT USER 'bob'",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL SOFT CONNECTION 10",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL SOFT CONNECTION USER 'bob'",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL SOFT QUERY 11",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL SOFT QUERY USER 'bob'",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
    {
        "KILL SOFT QUERY ID 12",
        mxs::sql::TYPE_WRITE,
        mxs::sql::OP_KILL
    },
};

void test_kill(Tester& tester)
{
    int i = 0;

    for (const std::string hardness : {"", "HARD", "SOFT"})
    {
        bool soft = hardness == "SOFT";

        for (const std::string type : {"", "CONNECTION", "QUERY", "QUERY ID"})
        {
            mxs::Parser::KillType qtype = type == "QUERY" ? mxs::Parser::KillType::QUERY :
                (type == "QUERY ID" ? mxs::Parser::KillType::QUERY_ID : mxs::Parser::KillType::CONNECTION);
            std::string id = std::to_string(i++);
            std::string sql_id = "KILL " + hardness + " " + type + " " + id;
            std::string sql_user = "KILL " + hardness + " " + type + " USER 'bob'";

            auto res_id = tester.get_kill(sql_id);

            expect(res_id.soft == soft, "Soft is not %s for: %s",
                   soft ? "true" : "false", sql_user.c_str());
            expect(res_id.user == false, "User should be false for: %s", sql_user.c_str());

            expect(res_id.type == qtype, "Type should be '%s', not '%s' for: %s",
                   mxs::parser::to_string(res_id.type),
                   mxs::parser::to_string(qtype),
                   sql_user.c_str());

            expect(res_id.target == id, "Target should be '%s', not '%s' for: %s",
                   id.c_str(), res_id.target.c_str(), sql_user.c_str());

            if (qtype != mxs::Parser::KillType::QUERY_ID)
            {
                auto res_user = tester.get_kill(sql_user);

                expect(res_user.soft == soft, "Soft is not %s for: %s",
                       soft ? "true" : "false", sql_user.c_str());
                expect(res_user.user == true, "User should be true for: %s", sql_user.c_str());

                expect(res_user.type == qtype, "Type should be '%s', not '%s' for: %s",
                       mxs::parser::to_string(res_user.type),
                       mxs::parser::to_string(qtype),
                       sql_user.c_str());

                expect(res_user.target == "bob", "Target should be 'bob', not '%s' for: %s",
                       res_user.target.c_str(), sql_user.c_str());
            }
        }
    }
}

void test_set_transaction(Tester& tester)
{
    for (std::string scope : {"", "SESSION", "GLOBAL"})
    {
        for (std::string level : {"READ COMMITTED", "READ UNCOMMITTED", "SERIALIZABLE", "REPEATABLE READ"})
        {
            for (std::string trx : {"READ ONLY", "READ WRITE"})
            {
                std::string isolation_level = "ISOLATION LEVEL " + level;
                std::vector<std::string> values {
                    trx, isolation_level, trx + ", " + isolation_level, isolation_level + ", " + trx
                };

                for (auto v : values)
                {
                    std::ostringstream ss;
                    ss << "SET " << scope << " TRANSACTION " << v;
                    std::string sql = ss.str();

                    auto op = tester.get_operation(sql);

                    expect(op == mxs::sql::OP_SET_TRANSACTION, "Expected %s, got %s",
                           mxs::sql::to_string(mxs::sql::OP_SET_TRANSACTION), mxs::sql::to_string(op));

                    auto type = tester.get_type(sql);
                    auto type_str = mxs::Parser::type_mask_to_string(type);

                    if (scope == "")
                    {
                        expect(type & mxs::sql::TYPE_NEXT_TRX,
                               "%s should be mxs::sql::TYPE_NEXT_TRX: %s", sql.c_str(), type_str.c_str());
                    }
                    else if (scope == "GLOBAL")
                    {
                        expect(type & mxs::sql::TYPE_GSYSVAR_WRITE,
                               "%s should be mxs::sql::TYPE_GSYSVAR_WRITE: %s", sql.c_str(), type_str.c_str());
                    }
                    else
                    {
                        expect(type & mxs::sql::TYPE_SESSION_WRITE, "Query should be QUERY_TYPE_SESSION_WRITE");
                    }

                    if (scope != "GLOBAL" && v.find(trx) != std::string::npos)
                    {
                        if (trx == "READ ONLY")
                        {
                            expect(type & mxs::sql::TYPE_READONLY,
                                   "%s should be mxs::sql::TYPE_READONLY: %s", sql.c_str(), type_str.c_str());
                        }
                        else
                        {
                            expect(type & mxs::sql::TYPE_READWRITE,
                                   "%s should be mxs::sql::TYPE_READWRITE: %s", sql.c_str(), type_str.c_str());
                        }
                    }
                    else
                    {
                        expect((type & (mxs::sql::TYPE_READONLY | mxs::sql::TYPE_READWRITE)) == 0,
                               "%s should not be mxs::sql::TYPE_READONLY or mxs::sql::TYPE_READWRITE: %s",
                               sql.c_str(), type_str.c_str());
                    }
                }
            }
        }
    }
}

int main(int argc, char** argv)
{
    int rc = 0;

    try
    {
        Tester tester("pp_sqlite", &MariaDBParser::Helper::get());

        for (const auto& t : test_cases)
        {
            std::string sql;
            uint32_t expected_type;
            mxs::sql::OpCode expected_op;
            std::tie(sql, expected_type, expected_op) = t;

            auto op = tester.get_operation(sql);
            expect(op == expected_op, "Expected %s, got %s for: %s",
                   mxs::sql::to_string(expected_op), mxs::sql::to_string(op), sql.c_str());

            auto type = tester.get_type(sql);
            auto type_str = mxs::Parser::type_mask_to_string(type);
            auto expected_type_str = mxs::Parser::type_mask_to_string(expected_type);

            expect(type == expected_type, "Expected %s, got %s for: %s",
                   expected_type_str.c_str(), type_str.c_str(), sql.c_str());
        }

        test_kill(tester);
        test_set_transaction(tester);

        rc = errors;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        rc = 1;
    }

    return rc;
}
