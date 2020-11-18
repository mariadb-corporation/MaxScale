#pragma once

#include <errno.h>
#include <string>
#include <set>
#include <vector>
#include <string>

#include <maxbase/ccdefs.hh>
#include <maxbase/string.hh>
#include <maxtest/mariadb_func.hh>

typedef std::set<std::string> StringSet;


class Nodes
{
public:
    virtual ~Nodes();

    /**
     * Sets up the nodes. *Must* be called before instance is used.
     *
     * @return True, if the instance could be setup, false otherwise.
     */
    virtual bool setup() = 0;

    const char* ip4(int i = 0) const;
    const char* ip6(int i = 0) const;
    const char* ip_private(int i = 0) const;

    /**
     * @brief Number of backend nodes
     */
    int N;

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

    const char* hostname(int i = 0) const;

    bool using_ipv6() const;

    const char* access_user(int i = 0) const;
    const char* access_homedir(int i = 0) const;
    const char* access_sudo(int i = 0) const;
    const char* sshkey(int i = 0) const;

    const std::string& prefix() const;

    /**
     * @brief mdbci_node_name
     * @param node
     * @return name of the node in MDBCI format
     */
    std::string mdbci_node_name(int node);

    /**
     * Generate the command line to execute a given command on the node via ssh.
     *
     * @param node Node index
     * @param cmd command to execute
     * @param sudo Execute command as root
     */
    std::string generate_ssh_cmd(int node, const std::string& cmd, bool sudo);

    // Simplified C++ version
    struct SshResult
    {
        int rc {-1};
        std::string output;
    };
    SshResult ssh_output(const std::string& cmd, int node = 0, bool sudo = true);

    /**
     * @brief executes shell command on the node using ssh
     * @param index number of the node (index)
     * @param ssh command to execute
     * @param sudo if true the command is executed with root privelegues
     * @return exit code of the coomand
     */
    int ssh_node(int node, const char* ssh, bool sudo);
    int ssh_node(int node, const std::string& ssh, bool sudo)
    {
        return ssh_node(node, ssh.c_str(), sudo);
    }
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
     * @return True if node is ok, false if start failed
     */
    bool check_nodes();

    /**
     * @brief read_basic_env Read IP, sshkey, etc - common parameters for all kinds of nodes
     * @return 0 in case of success
     */
    int read_basic_env();

    /**
     * @brief get_nc_item Find variable in the MDBCI network_config file
     * @param item_name Name of the variable
     * @return value of variable or empty value if not found
     */
    std::string get_nc_item(const char* item_name);

    /**
     * Calculate the number of nodes described in the network config file
     * @return Number of nodes
     */
    int get_N();

    /**
     * @brief start_vm Start virtual machine
     * @param node Node number
     * @return 0 in case of success
     */
    int start_vm(int node);

    /**
     * @brief stop_vm Stop virtual machine
     * @param node Node number
     * @return 0 in case of success
     */
    int stop_vm(int node);

protected:
    bool use_ipv6 {false}; /**< Default to ipv6-addresses */

    Nodes(const char* prefix,
          const std::string& network_config,
          bool verbose);

    void init_ssh_masters();

private:
    static constexpr int max_nodes {30};

    std::string m_prefix;                   /**< Name of backend setup (e.g. 'repl' or 'galera') */

    std::string m_ip4[max_nodes] {};        /**< IPv4-addresses for every backend node */
    std::string m_ip6[max_nodes] {};        /**< IPv6-addresses for every backend node */
    std::string m_ip_private[max_nodes] {}; /**< Private IP-addresses for every backend node (for AWS) */
    std::string m_hostname[max_nodes] {};   /**< Hostnames for every backend node */

    std::string m_start_vm_command[max_nodes] {}; /**< Command to resume VM */
    std::string m_stop_vm_command[max_nodes] {};  /**< Command to suspend VM */

    std::string network_config; /**< Contents of MDBCI network_config file */

    std::string m_access_user[max_nodes] {};    /**< Unix user name to access nodes via ssh */
    std::string m_access_homedir[max_nodes] {}; /**< home directory of access_user */
    std::string m_access_sudo[max_nodes] {};    /**< empty or "sudo " */

    std::string m_sshkey[max_nodes] {}; /**< Path to ssh key for every backend node */

    bool check_node_ssh(int node);

    // The returned handle must be closed with pclose
    FILE* open_ssh_connection(int node);

    std::vector<FILE*> m_ssh_connections;
};
