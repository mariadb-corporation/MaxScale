#pragma once

/**
 * @brief execute_cmd Execute shell command
 * @param cmd Command line
 * @param res Pointer to variable that will contain command console output (stdout)
 * @return Process exit code
 */
int execute_cmd(char * cmd, char ** res);

namespace jdbc
{
enum class ConnectorVersion
{
    MARIADB_250, MARIADB_270, MYSQL606
};

std::string to_string(ConnectorVersion vrs);

struct Result
{
    bool success {false};
    std::string output;
};
Result test_connection(ConnectorVersion vrs, const std::string& host, int port,
                       const std::string& user, const std::string& pass1, const std::string& pass2,
                       const std::string& query);

Result test_connection(ConnectorVersion vrs, const std::string& host, int port,
                       const std::string& user, const std::string& passwd,
                       const std::string& query);
}