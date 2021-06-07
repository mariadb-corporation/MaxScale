#include <iostream>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <maxbase/format.hh>
#include <maxtest/execute_cmd.hh>
#include <maxtest/test_dir.hh>

using namespace std;


int execute_cmd(char* cmd, char** res)
{
    char* result;
    FILE* output = popen(cmd, "r");
    if (output == NULL)
    {
        printf("Error opening ssh %s\n", strerror(errno));
        return -1;
    }
    char buffer[10240];
    size_t rsize = sizeof(buffer);
    result = (char*)calloc(rsize, sizeof(char));

    while (fgets(buffer, sizeof(buffer), output))
    {
        result = (char*)realloc(result, sizeof(buffer) + rsize);
        rsize += sizeof(buffer);
        strcat(result, buffer);
    }

    * res = result;

    int return_code = pclose(output);
    if (WIFEXITED(return_code))
    {
        return WEXITSTATUS(return_code);
    }
    else
    {
        return -1;
    }
}

namespace jdbc
{

Result test_connection(ConnectorVersion vrs, const std::string& host, int port,
                       const std::string& user, const std::string& pass1, const std::string& pass2,
                       const std::string& query)
{
    Result rval;
    string protocol = (vrs == ConnectorVersion::MARIADB_250 || vrs == ConnectorVersion::MARIADB_270) ? "mariadb" :
                      "mysql";
    auto url = mxb::string_printf("jdbc:%s://%s:%i/?user=%s&password=%s",
                                  protocol.c_str(), host.c_str(), port, user.c_str(), pass1.c_str());

    if (!pass2.empty())
    {
        url += "&password2=" + pass2;
    }
    string jarname;
    switch (vrs)
    {
    case ConnectorVersion::MARIADB_250:
        jarname = "jdbc_tool_mariadb_2.5.0.jar";
        break;
    case ConnectorVersion::MARIADB_270:
        jarname = "jdbc_tool_mariadb_2.7.0.jar";
        break;
    case ConnectorVersion::MYSQL606:
        jarname = "jdbc_tool_mysql_6.0.6.jar";
        url += "&serverTimezone=UTC"; // the MySQL-connector has trouble with some timezones
        break;
    }
    auto java_cmd = mxb::string_printf("java -jar %s/jdbc_tool/%s \"%s\"",
                                       mxt::SOURCE_DIR, jarname.c_str(), url.c_str());
    if (!query.empty())
    {
        java_cmd += " \"" + query + "\"";
    }
    auto process = popen(java_cmd.c_str(), "r");
    if (process)
    {
        char buffer[512] {0};
        string output;

        while (fgets(buffer, sizeof(buffer), process))
        {
            output += buffer;
        }

        int rc = pclose(process);
        rval.output = std::move(output);
        if (rc == 0)
        {
            rval.success = true;
        }
    }
    return rval;
}

Result test_connection(ConnectorVersion vrs, const std::string& host, int port,
                       const std::string& user, const std::string& passwd,
                       const std::string& query)
{
    return test_connection(vrs, host, port, user, passwd, "", query);
}

std::string to_string(ConnectorVersion vrs)
{
    string rval;
    switch (vrs)
    {
    case ConnectorVersion::MARIADB_250:
        rval = "MariaDB 2.5.0";
        break;
    case ConnectorVersion::MARIADB_270:
        rval = "MariaDB 2.7.0";
        break;
    case ConnectorVersion::MYSQL606:
        rval = "MySQL 6.0.6";
        break;
    }
    return rval;
}
}
