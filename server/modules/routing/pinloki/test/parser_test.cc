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

    void show_slave_status(bool all) override
    {
        if (all)
        {
            result << "SHOW ALL SLAVES STATUS";
        }
        else
        {
            result << "SHOW SLAVE STATUS";
        }
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

    void master_gtid_wait(const std::string& gtid, int timeout) override
    {
        result << "MASTER_GTID_WAIT " << gtid << " " << timeout;
    }

    void purge_logs(const std::string& up_to) override
    {
        result << "PURGE BINARY LOGS TO " << up_to;
    }

    void error(const std::string& err) override
    {
        result << "ERROR";
    }
} handler;

std::vector<std::pair<std::string, std::string>> tests =
{
    {
        "SELECT hello", "SELECT hello"
    },
    {
        "SELECT 'hello'", "SELECT hello"
    },
    {
        "SELECT \"hello\"", "SELECT hello"
    },
    {
        "SELECT 1", "SELECT 1"
    },
    {
        "SELECT 1;", "SELECT 1"     // MXS-3148
    },
    {
        "SELECT 1.5", "SELECT 1.5"
    },
    {
        "SELECT 1, 2", "SELECT 1,2"
    },
    {
        "SELECT unix_timestamp()", "SELECT unix_timestamp()"
    },
    {
        "SET a  =  1", "SET a=1"
    },
    {
        "SET a = 1, b = 2", "SET a=1SET b=2"
    },
    {
        "SET GLOBAL gtid_slave_pos = '1-1-1'", "SET gtid_slave_pos=1-1-1"
    },
    {
        "SET @@global.gtid_slave_pos = '1-1-1'", "SET gtid_slave_pos=1-1-1"
    },
    {
        "SET NAMES latin1", "SET NAMES=latin1"
    },
    {
        "SET NAMES utf8mb4", "SET NAMES=utf8mb4"
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
        "RESET SLAVE 'a'", "RESET SLAVE"
    },
    {
        "SHOW VARIABLES LIKE 'Server_id'", "SHOW VARIABLES LIKE Server_id"
    },
    {
        "RESET SLAVE ''", "RESET SLAVE"
    },
    {
        "PURGE MASTER LOGS TO 'binlog.000001'", "PURGE BINARY LOGS TO binlog.000001"
    },
    {
        "PURGE BINARY LOGS TO 'binlog.000001'", "PURGE BINARY LOGS TO binlog.000001"
    },
    {
        "SELECT MASTER_GTID_WAIT('0-1-1', 10)", "MASTER_GTID_WAIT 0-1-1 10"
    },
    {
        "SELECT MASTER_GTID_WAIT('0-1-1')", "MASTER_GTID_WAIT 0-1-1 0"
    },
    {
        "SHOW SLAVE STATUS", "SHOW SLAVE STATUS"
    },
    {
        "SHOW MASTER STATUS", "SHOW MASTER STATUS"
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
