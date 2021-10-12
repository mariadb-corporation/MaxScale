#include <iostream>
#include <stdexcept>

#include <maxbase/assert.h>
#include <maxscale/paths.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/modutil.hh>

using namespace std::literals::string_literals;

int errors = 0;

#define expect(a, fmt, ...)                    \
    do{if (!(a)) {                             \
           const char* what = #a;              \
           printf(fmt, ##__VA_ARGS__);         \
           printf("\n");                       \
           ++errors;                           \
       }}while(false)

class Tester
{
public:
    Tester(const char* query_classifier)
    {
        mxs::set_datadir("/tmp");
        mxs::set_langdir(".");
        mxs::set_process_datadir("/tmp");

        if (!mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            throw std::runtime_error("Failed to initialize the log");
        }

        m_qc = load_classifier(query_classifier);

        if (!m_qc)
        {
            throw std::runtime_error("Failed to load "s + query_classifier);
        }
    }

    ~Tester()
    {
        if (m_qc)
        {
            qc_unload(m_qc);
        }
    }

    qc_query_op_t get_operation(const std::string& sql)
    {
        mxs::Buffer buffer(modutil_create_query(sql.c_str()));
        int32_t op = QUERY_OP_UNDEFINED;

        if (m_qc->qc_get_operation(buffer.get(), &op) != QC_RESULT_OK)
        {
            std::cout << "failed to get operation for: " << sql << std::endl;
        }

        return (qc_query_op_t)op;
    }

    uint32_t get_type(const std::string& sql)
    {
        mxs::Buffer buffer(modutil_create_query(sql.c_str()));
        uint32_t type = 0;

        if (m_qc->qc_get_type_mask(buffer.get(), &type) != QC_RESULT_OK)
        {
            std::cout << "failed to get type for: " << sql << std::endl;
        }

        return type;
    }

private:

    QUERY_CLASSIFIER* load_classifier(const char* name)
    {
        std::string libdir = "../"s + name;
        mxs::set_libdir(libdir.c_str());
        QUERY_CLASSIFIER* pClassifier = qc_load(name);

        if (pClassifier)
        {
            const char* args = "parse_as=10.3,log_unrecognized_statements=1";

            if (pClassifier->qc_setup(QC_SQL_MODE_DEFAULT, args) != QC_RESULT_OK
                || pClassifier->qc_thread_init() != QC_RESULT_OK)
            {
                std::cerr << "error: Could not setup or init classifier " << name << "." << std::endl;
                qc_unload(pClassifier);
                pClassifier = nullptr;
            }
            else
            {
                uint64_t version = 10 * 1000 * 3 * 100;
                pClassifier->qc_set_server_version(version);
            }
        }
        else
        {
            std::cerr << "error: Could not load classifier " << name << "." << std::endl;
        }

        return pClassifier;
    }

    QUERY_CLASSIFIER* m_qc = nullptr;
};

static std::vector<std::tuple<std::string, uint32_t, qc_query_op_t>> test_cases
{
    {
        "select sleep(2);",
        QUERY_TYPE_READ,
        QUERY_OP_SELECT
    },
    {
        "select * from tst where lname like '%e%' order by fname;",
        QUERY_TYPE_READ,
        QUERY_OP_SELECT
    },
    {
        "insert into tst values ('Jane','Doe'),('Daisy','Duck'),('Marie','Curie');",
        QUERY_TYPE_WRITE,
        QUERY_OP_INSERT
    },
    {
        "update tst set fname='Farmer', lname='McDonald' where lname='%Doe' and fname='John';",
        QUERY_TYPE_WRITE,
        QUERY_OP_UPDATE
    },
    {
        "create table tmp as select * from t1;",
        QUERY_TYPE_WRITE,
        QUERY_OP_CREATE
    },
    {
        "create temporary table tmp as select * from t1;",
        QUERY_TYPE_WRITE | QUERY_TYPE_CREATE_TMP_TABLE,
        QUERY_OP_CREATE
    },
    {
        "select @@server_id;",
        QUERY_TYPE_READ | QUERY_TYPE_SYSVAR_READ,
        QUERY_OP_SELECT
    },
    {
        "select @OLD_SQL_NOTES;",
        QUERY_TYPE_READ | QUERY_TYPE_USERVAR_READ,
        QUERY_OP_SELECT
    },
    {
        "SET autocommit=1;",
        QUERY_TYPE_SESSION_WRITE | QUERY_TYPE_GSYSVAR_WRITE | QUERY_TYPE_ENABLE_AUTOCOMMIT
        | QUERY_TYPE_COMMIT,
        QUERY_OP_SET
    },
    {
        "SET autocommit=0;",
        QUERY_TYPE_SESSION_WRITE | QUERY_TYPE_GSYSVAR_WRITE | QUERY_TYPE_BEGIN_TRX
        | QUERY_TYPE_DISABLE_AUTOCOMMIT,
        QUERY_OP_SET
    },
    {
        "BEGIN;",
        QUERY_TYPE_BEGIN_TRX,
        QUERY_OP_UNDEFINED
    },
    {
        "ROLLBACK;",
        QUERY_TYPE_ROLLBACK,
        QUERY_OP_UNDEFINED
    },
    {
        "COMMIT;",
        QUERY_TYPE_COMMIT,
        QUERY_OP_UNDEFINED
    },
    {
        "use X;",
        QUERY_TYPE_SESSION_WRITE,
        QUERY_OP_CHANGE_DB
    },
    {
        "select last_insert_id();",
        QUERY_TYPE_READ | QUERY_TYPE_MASTER_READ,
        QUERY_OP_SELECT
    },
    {
        "select @@last_insert_id;",
        QUERY_TYPE_READ | QUERY_TYPE_MASTER_READ,
        QUERY_OP_SELECT
    },
    {
        "select @@identity;",
        QUERY_TYPE_READ | QUERY_TYPE_MASTER_READ,
        QUERY_OP_SELECT
    },
    {
        "select if(@@hostname='box02','prod_mariadb02','n');",
        QUERY_TYPE_READ | QUERY_TYPE_SYSVAR_READ,
        QUERY_OP_SELECT
    },
    {
        "select next value for seq1;",
        QUERY_TYPE_READ | QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "select nextval(seq1);",
        QUERY_TYPE_READ | QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "select seq1.nextval;",
        QUERY_TYPE_READ | QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT GET_LOCK('lock1',10);",
        QUERY_TYPE_READ | QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT IS_FREE_LOCK('lock1');",
        QUERY_TYPE_READ | QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT IS_USED_LOCK('lock1');",
        QUERY_TYPE_READ | QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT RELEASE_LOCK('lock1');",
        QUERY_TYPE_READ | QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "deallocate prepare select_stmt;",
        QUERY_TYPE_DEALLOC_PREPARE,
        QUERY_OP_UNDEFINED
    },
    {
        "SELECT a FROM tbl FOR UPDATE;",
        QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT a INTO OUTFILE 'out.txt';",
        QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT a INTO DUMPFILE 'dump.txt';",
        QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT a INTO @var;",
        QUERY_TYPE_GSYSVAR_WRITE,
        QUERY_OP_SELECT
    },
    {
        "select timediff(cast('2004-12-30 12:00:00' as time), '12:00:00');",
        QUERY_TYPE_READ,
        QUERY_OP_SELECT
    },
    {
        "(select 1 as a from t1) union all (select 1 from dual) limit 1;",
        QUERY_TYPE_READ,
        QUERY_OP_SELECT
    },
    {
        "SET @saved_cs_client= @@character_set_client;",
        QUERY_TYPE_SESSION_WRITE | QUERY_TYPE_USERVAR_WRITE,
        QUERY_OP_SET
    },
    {
        "SELECT 1 AS c1 FROM t1 ORDER BY ( SELECT 1 AS c2 FROM t1 GROUP BY GREATEST(LAST_INSERT_ID(), t1.a) ORDER BY GREATEST(LAST_INSERT_ID(), t1.a) LIMIT 1);",
        QUERY_TYPE_READ | QUERY_TYPE_MASTER_READ,
        QUERY_OP_SELECT
    },
    {
        "SET PASSWORD FOR 'user'@'10.0.0.1'='*C50EB75D7CB4C76B5264218B92BC69E6815B057A';",
        QUERY_TYPE_WRITE,
        QUERY_OP_SET
    },
    {
        "SELECT UTC_TIMESTAMP();",
        QUERY_TYPE_READ,
        QUERY_OP_SELECT
    },
    {
        "SELECT COUNT(IF(!c.ispackage, 1, NULL)) as cnt FROM test FOR UPDATE;",
        QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT handler FROM abc FOR UPDATE;",
        QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT * FROM test LOCK IN SHARE MODE;",
        QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },
    {
        "SELECT * FROM test FOR SHARE;",
        QUERY_TYPE_WRITE,
        QUERY_OP_SELECT
    },

    // MXS-3377: Parsing of KILL queries
    {
        "KILL 1",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL CONNECTION 2",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL CONNECTION USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL QUERY 3",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL QUERY USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL QUERY ID 4",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL QUERY ID USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL HARD 5",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL HARD USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL HARD CONNECTION 6",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL HARD CONNECTION USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL HARD QUERY 7",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL HARD QUERY USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL HARD QUERY ID 8",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL HARD QUERY ID USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL SOFT 9",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL SOFT USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL SOFT CONNECTION 10",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL SOFT CONNECTION USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL SOFT QUERY 11",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL SOFT QUERY USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL SOFT QUERY ID 12",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
    {
        "KILL SOFT QUERY ID USER 'bob'",
        QUERY_TYPE_WRITE,
        QUERY_OP_KILL
    },
};

int main(int argc, char** argv)
{
    int rc = 0;

    try
    {
        Tester tester("qc_sqlite");

        for (const auto& t : test_cases)
        {
            std::string sql;
            uint32_t expected_type;
            qc_query_op_t expected_op;
            std::tie(sql, expected_type, expected_op) = t;

            auto op = tester.get_operation(sql);
            expect(op == expected_op, "Expected %s, got %s for: %s",
                   qc_op_to_string(expected_op), qc_op_to_string(op), sql.c_str());

            auto type = tester.get_type(sql);
            char* type_str = qc_typemask_to_string(type);
            char* expected_type_str = qc_typemask_to_string(expected_type);

            expect(type == expected_type, "Expected %s, got %s for: %s",
                   expected_type_str, type_str, sql.c_str());

            free(type_str);
            free(expected_type_str);
        }

        rc = errors;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        rc = 1;
    }

    return rc;
}
