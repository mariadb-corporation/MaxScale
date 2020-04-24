#include "../parser.hh"

#include <iostream>
#include <sstream>

std::ostringstream result;

struct DebugHandler : public pinloki::parser::Handler
{

    void select(const std::vector<std::string>& values) override
    {
        result << "SELECT " << mxb::join(values);
    }

    void set(const std::string& key, const std::string& value) override
    {
        result << "SET " << key << "=" << value;
    }

    void change_master_to(const pinloki::parser::ChangeMasterValues& values) override
    {
        result << "CHANGE MASTER TO";
    }

    void start_slave() override
    {
        result << "START SLAVE";
    }

    void stop_slave() override
    {
        result << "STOP SLAVE";
    }

    void reset_slave() override
    {
        result << "RESET SLAVE";
    }

    void show_slave_status() override
    {
        result << "SHOW SLAVE STATUS";
    }

    void show_master_status() override
    {
        result << "SHOW MASTER STATUS";
    }

    void show_binlogs() override
    {
        result << "SHOW BINLOGS";
    }

    void show_variables(const std::string& like) override
    {
        result << "SHOW VARIABLES" << (like.empty() ? "" : " LIKE ") << like;
    }

    void flush_logs() override
    {
        result << "FLUSH LOGS";
    }

    void purge_logs() override
    {
        result << "PURGE LOGS";
    }

    void error(const std::string& err) override
    {
        result << "ERROR";
    }
} handler;

std::vector<std::pair<std::string, std::string>> tests =
{
    {
        "SELECT 1", "SELECT 1"
    },
    {
        "SELECT 1, 2", "SELECT 1,2"
    },
    {
        "SET a  =  1", "SET a=1"
    },
    {
        "SET a = 1, b = 2", "SET a=1SET b=2"
    },
    {
        "SET NAMES latin1", ""      // Ignored
    },
    {
        "CHANGE MASTER TO master_host='127.0.0.1', master_port=3306", "CHANGE MASTER TO"
    },
    {
        "STOP SLAVE", "STOP SLAVE"
    },
    {
        "START SLAVE", "START SLAVE"
    },
    {
        "RESET SLAVE", "RESET SLAVE"
    },
    {
        "RESET SLAVE ALL", "ERROR"
    },
    {
        "RESET SLAVE 'a'", "ERROR"
    },
    {
        "SHOW VARIABLES LIKE 'Server_id'", "SHOW VARIABLES LIKE Server_id"
    },
    {
        "RESET SLAVE ''", "ERROR"
    },
    {
        "FLUSH LOGS", "FLUSH LOGS"
    },
    {
        "PURGE LOGS", "PURGE LOGS"
    },
};

int main(int argc, char** argv)
{
    int err = 0;

    for (const auto& t : tests)
    {
        pinloki::parser::parse(t.first, &handler);

        if (result.str() != t.second)
        {
            err++;
            std::cout << "Expected '" << t.second << "' but got '" << result.str() << "'" << std::endl;
        }

        result.str("");
    }

    return err;
}
