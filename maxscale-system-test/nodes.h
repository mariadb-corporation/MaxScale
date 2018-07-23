#ifndef NODES_H
#define NODES_H

#include <errno.h>
#include <string>
#include "mariadb_func.h"
#include <set>
#include <string>

#include <maxbase/ccdefs.hh>

typedef std::set<std::string> StringSet;


class Nodes
{
public:
    Nodes();

    char IP[256][1024];
    /**
     * @brief  private IP address strings for every backend node (for AWS)
     */

    char IP_private[256][1024];
    /**
     * @brief  IP address strings for every backend node (IPv6)
     */
    char IP6[256][1024];

    /**
     * @brief use_ipv6 If true IPv6 addresses will be used to connect Maxscale and backed
     * Also IPv6 addresses go to maxscale.cnf
     */
    bool use_ipv6;

    /**
     * @brief  Path to ssh key for every backend node
     */
    char sshkey[256][4096];

    /**
     * @brief Number of backend nodes
     */
    int N;

    /**
     * @brief     name of backend setup (like 'repl' or 'galera')
     */
    char prefix[16];

    /**
     * @brief access_user Unix users name to access nodes via ssh
     */
    char access_user[256][256];

    /**
     * @brief access_sudo empty if sudo is not needed or "sudo " if sudo is needed.
     */
    char access_sudo[256][64];

    /**
     * @brief access_homedir home directory of access_user
     */
    char access_homedir[256][256];

    char hostname[256][1024];

    /**
     * @brief stop_vm_command Command to suspend VM
     */
    char stop_vm_command[256][1024];
    /**

     * @brief start_vm_command Command to resume VM
     */
    char start_vm_command[256][1024];

    /**
     * @brief   User name to access backend nodes
     */
    char user_name[256];
    /**
     * @brief   Password to access backend nodes
     */
    char password[256];

    /**
     * @brief Verbose command output
     */
    bool verbose;

    /**
     * @brief Get IP address
     *
     * @return The current IP address
     */
    const char* ip(int i = 0) const;

    /**
     * @brief Generate command line to execute command on the node via ssh
     * @param cmd result
     * @param index index number of the node (index)
     * @param ssh command to execute
     * @param sudo if true the command is executed with root privelegues
     */
    void generate_ssh_cmd(char * cmd, int node, const char *ssh, bool sudo);

    /**
     * @brief executes shell command on the node using ssh
     * @param index number of the node (index)
     * @param ssh command to execute
     * @param sudo if true the command is executed with root privelegues
     * @param pointer to variable to store process exit code
     * @return output of the command
     */
    char *ssh_node_output_f(int node, bool sudo, int* exit_code, const char* format, ...) mxb_attribute((format(printf, 5, 6)));
    char *ssh_node_output(int node, const char *ssh, bool sudo, int *exit_code);

    // Simplified C++ version
    std::pair<int, std::string> ssh_output(std::string ssh, int node = 0, bool sudo = true)
    {
        int rc;
        char* out = ssh_node_output(node, ssh.c_str(), sudo, &rc);
        std::string rval(out);
        free(out);
        return {rc, rval};
    }

    /**
     * @brief executes shell command on the node using ssh
     * @param index number of the node (index)
     * @param ssh command to execute
     * @param sudo if true the command is executed with root privelegues
     * @return exit code of the coomand
     */
    int ssh_node(int node, const char *ssh, bool sudo);
    int ssh_node_f(int node, bool sudo, const char* format, ...) mxb_attribute((format(printf, 4, 5)));

    /**
     * @brief Copy a local file to the Node i machine
     * @param src Source file on the local filesystem
     * @param dest Destination file on the remote file system
     * @param i Node index
     * @return exit code of the system command or 1 in case of i > N
     */
    int copy_to_node_legacy(const char* src, const char* dest, int i = 0);
    int copy_to_node(int i, const char* src, const char* dest);

    /**
     * @brief Copy a local file to the Node i machine
     * @param src Source file on the remote filesystem
     * @param dest Destination file on the local file system
     * @param i Node index
     * @return exit code of the system command or 1 in case of i > N
     */
    int copy_from_node_legacy(const char* src, const char* dest, int i);
    int copy_from_node(int i, const char* src, const char* dest);

    /**
     * @brief Check node via ssh and restart it if it is not resposible
     * @param node Node index
     * @return 0 if node is ok, 1 if start failed
     */
    int check_nodes();

    /**
     * @brief read_basic_env Read IP, sshkey, etc - common parameters for all kinds of nodes
     * @return 0 in case of success
     */
    int read_basic_env();

private:
    int check_node_ssh(int node);

};

#endif // NODES_H
