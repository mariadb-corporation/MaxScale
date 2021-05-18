#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <maxtest/ccdefs.hh>
#include <maxbase/string.hh>
#include <maxtest/mariadb_func.hh>

typedef std::set<std::string> StringSet;

namespace maxtest
{
class SharedData;
class TestLogger;

struct CmdResult
{
    int         rc{-1};
    std::string output;
};

using NetworkConfig = std::map<std::string, std::string>;

class VMNode
{
public:
    VMNode(SharedData& shared, const std::string& name);
    ~VMNode();
    VMNode(const VMNode&) = delete;

    bool init_ssh_master();
    void close_ssh_master();

    enum class CmdPriv
    {
        NORMAL, SUDO
    };

    /**
     * Run a command on the VM, either through ssh or local terminal. No output.
     *
     * @param cmd Command string
     * @param priv Sudo or normal user
     * @return Return code
     */
    int run_cmd(const std::string& cmd, CmdPriv priv = CmdPriv::NORMAL);

    int run_cmd_sudo(const std::string& cmd);

    /**
     * Run a command on the VM, either through ssh or local terminal. Fetches output.
     *
     * @param cmd Command string
     * @param priv Sudo or normal user
     * @return Return code and command output
     */
    mxt::CmdResult run_cmd_output(const std::string& cmd, CmdPriv priv = CmdPriv::NORMAL);

    mxt::CmdResult run_cmd_output_sudo(const std::string& cmd);

    bool configure(const mxt::NetworkConfig& nwconfig);

    /**
     * Write node network info to environment variables. This is mainly needed by script-type tests.
     */
    void write_node_env_vars();

    void set_local();

    /**
     * Copy a local file to the node.
     *
     * @param src Source file on the local filesystem
     * @param dest Destination file on the remote file system
     * @return True on success
     */
    bool copy_to_node(const std::string& src, const std::string& dest);

    /**
     * Copy a local file to the node with sudo privs. Required when the destination directory
     * is restricted. Implemented by first scp:ing the file to home dir, then copying it to destination
     * and finally deleting the temporary.
     *
     * @param src Source file on the local filesystem
     * @param dest Destination file on the remote file system
     * @return True on success
     */
    bool copy_to_node_sudo(const std::string& src, const std::string& dest);

    bool copy_from_node(const std::string& src, const std::string& dest);

    const char* ip4() const;
    const char* priv_ip() const;
    const char* hostname() const;
    const char* access_user() const;
    const char* access_homedir() const;
    const char* access_sudo() const;
    const char* sshkey() const;

    const std::string& ip4s() const;
    const std::string& ip6s() const;

    TestLogger& log();
    bool        is_remote() const;

    const std::string m_name;       /**< E.g. "node_001" */

private:
    std::string get_nc_item(const mxt::NetworkConfig& nwconfig, const std::string& search_key);
    bool        verbose() const;

    std::string m_ip4;          /**< IPv4-address */
    std::string m_ip6;          /**< IPv6-address */
    std::string m_private_ip;   /**< Private IP-address for AWS */
    std::string m_hostname;     /**< Hostname */

    std::string m_username; /**< Unix user name to access nodes via ssh */
    std::string m_homedir;  /**< Home directory of username */
    std::string m_sudo;     /**< empty or "sudo " */
    std::string m_sshkey;   /**< Path to ssh key */

    enum class NodeType
    {
        LOCAL, REMOTE
    };

    NodeType    m_type{NodeType::REMOTE};       /**< SSH only used on remote nodes */
    std::string m_ssh_cmd_p1;                   /**< Start of remote command string */
    FILE*       m_ssh_master_pipe{nullptr};     /**< Master ssh pipe. Kept open for ssh multiplex */
    SharedData& m_shared;
};
}

class Nodes
{
public:
    virtual ~Nodes() = default;

    const char* ip_private(int i = 0) const;

    bool verbose() const;

    /**
     * @brief mdbci_node_name
     * @param node
     * @return name of the node in MDBCI format
     */
    std::string mdbci_node_name(int node);

    mxt::CmdResult ssh_output(const std::string& cmd, int node = 0, bool sudo = true);

    /**
     * @brief executes shell command on the node using ssh
     * @param index number of the node (index)
     * @param ssh command to execute
     * @param sudo if true the command is executed with root privelegues
     * @return exit code of the coomand
     */
    int ssh_node(int node, const std::string& ssh, bool sudo);
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

    void write_env_vars();

    int n_nodes() const;

protected:
    mxt::SharedData& m_shared;

    Nodes(mxt::SharedData* shared);

    const char* ip4(int i) const;
    const char* ip6(int i) const;
    const char* hostname(int i) const;

    const char* access_user(int i) const;
    const char* access_homedir(int i) const;
    const char* access_sudo(int i) const;
    const char* sshkey(int i) const;

    virtual bool init_ssh_masters();

    mxt::VMNode*       node(int i);
    const mxt::VMNode* node(int i) const;

    void clear_vms();
    bool add_node(const mxt::NetworkConfig& nwconfig, const std::string& name);

private:
    std::vector<std::unique_ptr<mxt::VMNode>> m_vms;
};
