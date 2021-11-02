/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <sstream>
#include <random>
#include <thread>
#include <vector>
#include <unistd.h>
#include <mysql.h>

using namespace std;

namespace
{

const char* ZOPTIONS="t:r:c:h:P:u:p:";

const int DEFAULT_THREADS = 10;
const int DEFAULT_ROWS = 100;
const int DEFAULT_PERCENTAGE = 20;
const int DEFAULT_PORT = 4006;

void usage(ostream& out, const char* zProgram)
{
    out << "usage: " << zProgram << " [-t num] [-r num] [-c num] [-h host] [-P port] -u user [-p pwd]\n"
        << "\n"
        << "  -t num : Number of threads\n"
        << "  -r num : Number of rows in table\n"
        << "  -c num : Percentage of updates\n"
        << "  -h host: MaxScale host (default 127.0.0.1)\n"
        << "  -P port: MaxScale port (default 4006)\n"
        << "  -u user: User to connect with\n"
        << "  -p pwd : Password to use\n\n"
        << "Default: " << zProgram
        << " -t " << DEFAULT_THREADS
        << " -r " << DEFAULT_ROWS
        << " -c " << DEFAULT_PERCENTAGE
        << " -h 127.0.0.1 "
        << " -P " << DEFAULT_PORT
        << endl;
}

class MDB
{
public:
    class Exception : public std::runtime_error
    {
    public:
        explicit Exception(const char* zWhat)
            : std::runtime_error(zWhat)
        {
        }

        explicit Exception(const std::string& what)
            : std::runtime_error(what.c_str())
        {
        }
    };

    struct Config
    {
        Config(const std::string& host,
               int port,
               const std::string& user,
               const std::string& password,
               const std::string& db)
            : host(host)
            , port(port)
            , user(user)
            , password(password)
            , db(db)
        {
        }

        std::string host;
        int         port;
        std::string user;
        std::string password;
        std::string db;
    };

    MDB(const MDB&) = delete;
    MDB& operator = (const MDB&) = delete;

    MDB(const std::string& host,
        int port,
        const std::string& user,
        const std::string& password,
        const std::string& db = "")
        : MDB(Config(host, port, user, password, db))
    {
    }

    MDB(const Config& config)
        : m_config(config)
    {
        mysql_init(&m_mysql);
    }

    const Config& config() const
    {
        return m_config;
    }

    ~MDB()
    {
        mysql_close(&m_mysql);
    }

    bool try_connect()
    {
        bool rv = false;

        mysql_close(&m_mysql);
        mysql_init(&m_mysql);

        const char* zHost = m_config.host.c_str();
        int port = m_config.port;
        const char* zUser = m_config.user.c_str();
        const char* zPassword = !m_config.password.empty() ? m_config.password.c_str() : nullptr;
        const char* zDb = !m_config.db.empty() ? m_config.db.c_str() : nullptr;

        return mysql_real_connect(&m_mysql, zHost, zUser, zPassword, zDb, port, nullptr, 0) != nullptr;
    }

    bool try_query(const std::string& stmt)
    {
        return mysql_query(&m_mysql, stmt.c_str()) == 0;
    }

    void query(const std::string& stmt)
    {
        if (!try_query(stmt))
        {
            raise();
        }
    }

    void connect()
    {
        if (!try_connect())
        {
            raise();
        }
    }

    using Row = std::vector<std::string>;
    using Rows = std::vector<Row>;

    Rows result() const
    {
        Rows rows;
        MYSQL_RES* pRes = mysql_store_result(&m_mysql);

        if (pRes)
        {
            int nFields = mysql_num_fields(pRes);
            MYSQL_ROW pRow = mysql_fetch_row(pRes);

            while (pRow)
            {
                std::vector<std::string> row;
                for (int i = 0; i < nFields; ++i)
                {
                    row.push_back(pRow[i] ? pRow[i] : "");
                }
                rows.push_back(row);

                pRow = mysql_fetch_row(pRes);
            }

            mysql_free_result(pRes);
        }

        return rows;
    }

    std::string last_error() const
    {
        return mysql_error(&m_mysql);
    }

private:
    void raise(const char* zWhat)
    {
        throw Exception(zWhat);
    }

    void raise(const std::string& what)
    {
        raise(what.c_str());
    }

    void raise()
    {
        raise(last_error());
    }

private:
    mutable MYSQL m_mysql;
    Config        m_config;
};

void finish_db(MDB& mdb)
{
    mdb.query("DROP TABLE IF EXISTS test.cache_stress");
}

void setup_db(MDB& mdb, int nRows)
{
    finish_db(mdb);

    mdb.query("CREATE TABLE test.cache_stress (f INT, t INT)");

    mdb.query("BEGIN");

    for (int i = 0; i < nRows; ++i)
    {
        stringstream ss;
        ss << "INSERT INTO test.cache_stress VALUES (" << i << ", 0)";

        mdb.query(ss.str());
    }

    mdb.query("COMMIT");
}

void thread_run(int tid, MDB& mdb, int rows, int percentage)
{
    int cutoff = percentage * ((double)RAND_MAX - 1) / 100;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, rows - 1);

    while (true)
    {
        if (std::rand() < cutoff)
        {
            // UPDATE
            stringstream ss;
            ss << "UPDATE test.cache_stress SET t = ";
            ss << tid;
            ss << " WHERE f = ";
            ss << dis(gen);

            /*
            stringstream ss2;
            ss2 << tid << ": " << ss.str() << "\n";
            cout << ss2.str() << flush;
            */

            mdb.query(ss.str());
        }
        else
        {
            // SELECT
            int f = dis(gen);

            stringstream ss;
            ss << "SELECT * FROM test.cache_stress WHERE f = " << f;

            /*
            stringstream ss2;
            ss2 << tid << ": " << ss.str() << "\n";
            cout << ss2.str() << flush;
            */

            mdb.query(ss.str());
            mdb.result();
        }
    }
}

void thread_main(int tid, MDB::Config config, int rows, int percentage)
{
    MDB mdb(config);
    mdb.connect();

    try
    {
        thread_run(tid, mdb, rows, percentage);
    }
    catch (const std::exception& x)
    {
        stringstream ss;
        ss << tid << ": exception: " << x.what() << "\n";
        cout << ss.str() << flush;
    }
}

void run(MDB& mdb, int nThreads, int nRows, int nPercentage)
{
    vector<thread> threads;

    for (int i = 1; i <= nThreads; ++i)
    {
        threads.emplace_back(thread_main, i, mdb.config(), nRows, nPercentage);
    }

    for (int i = 0; i < nThreads; ++i)
    {
        threads[i].join();
    }
}

}

int main(int argc, char* argv[])
{
    int nThreads = 10;
    int nRows = 100;
    int nPercentage = DEFAULT_PERCENTAGE;
    const char* zHost = "127.0.0.1";
    int port = 4006;
    const char* zUser = nullptr;
    const char* zPassword = "";

    int opt;
    while ((opt = getopt(argc, argv, ZOPTIONS)) != -1)
    {
        switch (opt)
        {
        case 't':
            nThreads = atoi(optarg);
            break;

        case 'r':
            nRows = atoi(optarg);
            break;

        case 'c':
            nPercentage = atoi(optarg);
            break;

        case 'h':
            zHost = optarg;
            break;

        case 'P':
            port = atoi(optarg);
            break;

        case 'u':
            zUser = optarg;
            break;

        case 'p':
            zPassword = optarg;
            break;

        case '?':
            usage(cout, argv[0]);
            exit(EXIT_SUCCESS);
            break;

        default:
            usage(cerr, argv[0]);
            exit(EXIT_FAILURE);
            break;
        };
    }

    if (nThreads <= 0
        || nRows <= 1
        || (nPercentage < 0 || nPercentage > 100)
        || port <= 0
        || !zUser)
    {
        usage(cerr, argv[0]);
        exit(EXIT_FAILURE);
    }

    MDB mdb(zHost, port, zUser, zPassword);

    if (mdb.try_connect())
    {
        try
        {
            setup_db(mdb, nRows);
            run(mdb, nThreads, nRows, nPercentage);
            finish_db(mdb);
        }
        catch (const std::exception& x)
        {
            cerr << "error: Exception, " << x.what() << endl;
        }
    }
    else
    {
        cerr << "error: " << mdb.last_error() << endl;
    }
}
