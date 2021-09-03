#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <maxbase/format.hh>
#include <maxtest/execute_cmd.hh>
#include <maxtest/test_dir.hh>
#include <maxtest/nodes.hh>

using std::string;

namespace
{
const char lib_temp[] = "/tmp/pam_user_map.so";
const char pam_map_config_name[] = "pam_config_user_map";
const char pam_user_map_conf_dst[] = "/etc/security/user_map.conf";
}

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

namespace pam
{
void copy_user_map_lib(mxt::VMNode& source, mxt::VMNode& dst)
{
    // Copy the pam_user_map.so-file from one VM to another. This file is installed with the server,
    // but not with MaxScale. Depending on distro, the file may be in different places. Check both.
    string lib_source1 = "/usr/lib64/security/pam_user_map.so";
    string lib_source2 = "/usr/lib/security/pam_user_map.so";

    if (source.copy_from_node(lib_source1, lib_temp) || source.copy_from_node(lib_source2, lib_temp))
    {
        if (dst.copy_to_node(lib_temp, lib_temp))
        {
            dst.log().log_msg("pam_user_map.so copied to MaxScale VM.");
        }
        else
        {
            dst.log().add_failure("Failed to copy library '%s' to %s.",
                                  lib_temp, dst.name());
        }
    }
    else
    {
        source.log().add_failure("Failed to copy library '%s' or '%s' from %s to host machine.",
                                 lib_source1.c_str(), lib_source2.c_str(), source.name());
    }
}

void delete_user_map_lib(mxt::VMNode& dst)
{
    // Delete the library file from both the tester VM and destination VM (likely MaxScale).
    string del_lib_cmd = mxb::string_printf("rm -f %s", lib_temp);
    int rc = system(del_lib_cmd.c_str());
    dst.log().expect(rc == 0, "Command '%s' failed, error %i.", del_lib_cmd.c_str(), rc);
    auto res = dst.run_cmd_output_sudo(del_lib_cmd);
    dst.log().expect(res.rc == 0, "Command '%s' failed on %s: %s",
                     del_lib_cmd.c_str(), dst.name(), res.output.c_str());
    if (rc == 0 && res.rc == 0)
    {
        dst.log().log_msg("pam_user_map.so deleted on local machine and remote VM.");
    }
}

void copy_map_config(mxt::VMNode& vm)
{
    auto test_dir = mxt::SOURCE_DIR;
    string pam_map_config_path_src = mxb::string_printf("%s/authentication/%s",
                                                        test_dir, pam_map_config_name);
    string pam_map_config_path_dst = mxb::string_printf("/etc/pam.d/%s", pam_map_config_name);
    string pam_user_map_conf_src = mxb::string_printf("%s/authentication/user_map.conf", test_dir);
    vm.copy_to_node_sudo(pam_map_config_path_src, pam_map_config_path_dst);
    vm.copy_to_node_sudo(pam_user_map_conf_src, pam_user_map_conf_dst);
    vm.log().log_msg("PAM user mapping config files copied.");
}

void delete_map_config(mxt::VMNode& vm)
{
    string pam_map_config_path_dst = mxb::string_printf("/etc/pam.d/%s", pam_map_config_name);
    vm.delete_from_node(pam_map_config_path_dst);
    vm.delete_from_node(pam_user_map_conf_dst);
    vm.log().log_msg("PAM user mapping config files deleted.");
}
}
