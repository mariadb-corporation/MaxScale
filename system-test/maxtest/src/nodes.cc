#include <maxtest/nodes.hh>

#include <algorithm>
#include <cstring>
#include <future>
#include <functional>
#include <iostream>
#include <string>
#include <csignal>
#include <maxbase/format.hh>
#include <maxtest/envv.hh>
#include <maxtest/log.hh>

using std::string;
using std::move;

namespace
{
// Options given when running ssh from command line. The first line enables connection multiplexing,
// allowing repeated ssh-invocations to use an existing connection.
// Second line disables host ip and key checks.
const char ssh_opts[] = "-o ControlMaster=auto -o ControlPath=./maxscale-test-%r@%h:%p -o ControlPersist=yes "
                        "-o CheckHostIP=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "
                        "-o LogLevel=quiet ";
}

Nodes::Nodes(const string& prefix, SharedData* shared, const std::string& network_config)
    : m_shared(*shared)
    , m_prefix(prefix)
    , m_network_config(network_config)
{
}

Nodes::VMNode::VMNode(SharedData& shared, const string& name)
    : m_name(name)
    , m_shared(shared)
{
}

Nodes::VMNode::~VMNode()
{
    if (m_ssh_master_pipe)
    {
        fprintf(m_ssh_master_pipe, "exit\n");
        pclose(m_ssh_master_pipe);
    }
}

Nodes::VMNode::VMNode(Nodes::VMNode&& rhs)
    : m_ip4(move(rhs.m_ip4))
    , m_ip6(move(rhs.m_ip6))
    , m_private_ip(move(rhs.m_private_ip))
    , m_hostname(move(rhs.m_hostname))
    , m_username(move(rhs.m_username))
    , m_homedir(move(rhs.m_homedir))
    , m_sudo(move(rhs.m_sudo))
    , m_sshkey(move(rhs.m_sshkey))
    , m_ssh_master_pipe(rhs.m_ssh_master_pipe)
    , m_shared(rhs.m_shared)
{
    rhs.m_ssh_master_pipe = nullptr;
}

bool Nodes::check_node_ssh(int node)
{
    bool res = true;

    if (ssh_node(node, "ls > /dev/null", false) != 0)
    {
        std::cout << "Node " << node << " is not available" << std::endl;
        res = false;
    }

    return res;
}

bool Nodes::check_nodes()
{
    std::vector<std::future<bool>> f;

    for (int i = 0; i < N; i++)
    {
        f.push_back(std::async(std::launch::async, &Nodes::check_node_ssh, this, i));
    }

    return std::all_of(f.begin(), f.end(), std::mem_fn(&std::future<bool>::get));
}

bool Nodes::VMNode::init_ssh_master()
{
    if (m_ip4 == "127.0.0.1")
    {
        m_type = NodeType::LOCAL;
        return true;
    }

    m_ssh_cmd_p1 = mxb::string_printf("ssh -i %s %s %s@%s",
                                      m_sshkey.c_str(), ssh_opts,
                                      m_username.c_str(), m_ip4.c_str());

    // For initiating the master connection, just part1 is enough.
    FILE* instream = popen(m_ssh_cmd_p1.c_str(), "w");
    bool rval = false;
    if (instream)
    {
        m_ssh_master_pipe = instream;
        rval = true;
    }
    else
    {
        std::cout << m_name << ": popen() failed when forming master ssh connection\n";
    }
    return rval;
}

int Nodes::VMNode::run_cmd(const std::string& cmd, CmdPriv priv)
{
    bool verbose = m_shared.verbose;
    string opening_cmd;
    if (m_type == NodeType::LOCAL)
    {
        opening_cmd = "bash";
    }
    else
    {
        opening_cmd = m_ssh_cmd_p1;
        if (!verbose)
        {
            opening_cmd += " > /dev/null";
        }
    }
    if (verbose)
    {
        std::cout << opening_cmd << "\n";
    }

    // TODO: Is this 2-stage execution necessary? Could the command just be ran in one go?
    int rc = -1;
    FILE* pipe = popen(opening_cmd.c_str(), "w");
    if (pipe)
    {
        bool sudo = (priv == CmdPriv::SUDO);
        if (sudo)
        {
            fprintf(pipe, "sudo su -\n");
            fprintf(pipe, "cd /home/%s\n", m_username.c_str());
        }

        fprintf(pipe, "%s\n", cmd.c_str());
        if (sudo)
        {
            fprintf(pipe, "exit\n");    // Exits sudo
        }
        fprintf(pipe, "exit\n");    // Exits ssh / bash
        rc = pclose(pipe);
    }
    else
    {
        std::cout << m_name << ": popen() failed";
    }

    if (WIFEXITED(rc))
    {
        rc = WEXITSTATUS(rc);
    }
    else if (WIFSIGNALED(rc) && WTERMSIG(rc) == SIGHUP)
    {
        // SIGHUP appears to happen for SSH connections
        rc = 0;
    }
    else
    {
        std::cout << strerror(errno) << "\n";
        rc = 256;
    }
    return rc;
}

int Nodes::ssh_node(int node, const string& ssh, bool sudo)
{
    return m_vms[node].run_cmd(ssh, sudo ? VMNode::CmdPriv::SUDO : VMNode::CmdPriv::NORMAL);
}

bool Nodes::setup()
{
    std::vector<std::future<bool>> futures;
    futures.reserve(m_vms.size());
    for (auto& vm : m_vms)
    {
        auto func = [&vm]() {
                return vm.init_ssh_master();
            };
        futures.emplace_back(std::async(std::launch::async, func));
    }

    bool rval = true;
    for (auto& fut : futures)
    {
        if (!fut.get())
        {
            rval = false;
        }
    }
    return rval;
}

int Nodes::ssh_node_f(int node, bool sudo, const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    string sys = mxb::string_vprintf(format, valist);
    va_end(valist);
    return ssh_node(node, sys.c_str(), sudo);
}

int Nodes::copy_to_node(int i, const char* src, const char* dest)
{
    if (i >= N)
    {
        return 1;
    }
    char sys[strlen(src) + strlen(dest) + 1024];

    auto& vmnode = m_vms[i];
    auto& ip4 = vmnode.m_ip4;
    if (ip4 == "127.0.0.1")
    {
        sprintf(sys,
                "cp %s %s",
                src,
                dest);
    }
    else
    {
        sprintf(sys,
                "scp -q -r -i %s "
                "-o UserKnownHostsFile=/dev/null "
                "-o CheckHostIP=no "
                "-o ControlMaster=auto "
                "-o ControlPath=./maxscale-test-%%r@%%h:%%p "
                "-o ControlPersist=yes "
                "-o StrictHostKeyChecking=no "
                "-o LogLevel=quiet "
                "%s %s@%s:%s",
                vmnode.m_sshkey.c_str(),
                src,
                vmnode.m_username.c_str(),
                ip4.c_str(),
                dest);
    }
    if (verbose())
    {
        printf("%s\n", sys);
    }

    return system(sys);
}


int Nodes::copy_to_node_legacy(const char* src, const char* dest, int i)
{

    return copy_to_node(i, src, dest);
}

int Nodes::copy_from_node(int i, const char* src, const char* dest)
{
    if (i >= N)
    {
        return 1;
    }
    char sys[strlen(src) + strlen(dest) + 1024];
    auto& vmnode = m_vms[i];
    auto& ip4 = vmnode.m_ip4;
    if (ip4 == "127.0.0.1")
    {
        sprintf(sys,
                "cp %s %s",
                src,
                dest);
    }
    else
    {
        sprintf(sys,
                "scp -q -r -i %s -o UserKnownHostsFile=/dev/null "
                "-o StrictHostKeyChecking=no "
                "-o LogLevel=quiet "
                "-o CheckHostIP=no "
                "-o ControlMaster=auto "
                "-o ControlPath=./maxscale-test-%%r@%%h:%%p "
                "-o ControlPersist=yes "
                "%s@%s:%s %s",
                vmnode.m_sshkey.c_str(),
                vmnode.m_username.c_str(),
                ip4.c_str(),
                src,
                dest);
    }
    if (verbose())
    {
        printf("%s\n", sys);
    }

    return system(sys);
}

int Nodes::copy_from_node_legacy(const char* src, const char* dest, int i)
{
    return copy_from_node(i, src, dest);
}

int Nodes::read_basic_env()
{
    N = get_N();

    auto prefixc = m_prefix.c_str();
    m_vms.clear();
    m_vms.reserve(N);

    if ((N > 0) && (N < 255))
    {
        for (int i = 0; i < N; i++)
        {
            string node_name = mxb::string_printf("%s_%03d", prefixc, i);
            VMNode node(m_shared, node_name);

            if (node.configure(m_network_config))
            {
                m_vms.push_back(move(node));
            }
            else
            {
                break;
            }
        }
    }

    return 0;
}

bool Nodes::VMNode::configure(const std::string& network_config)
{
    auto& name = m_name;
    string field_network = name + "_network";

    bool success = false;
    string ip4 = get_nc_item(field_network, network_config);
    if (!ip4.empty())
    {
        m_ip4 = ip4;

        string field_network6 = name + "_network6";
        string field_private_ip = name + "_private_ip";
        string field_hostname = name + "_hostname";
        string field_keyfile = name + "_keyfile";
        string field_whoami = name + "_whoami";
        string field_access_sudo = name + "_access_sudo";

        string ip6 = get_nc_item(field_network6, network_config);
        m_ip6 = !ip6.empty() ? ip6 : m_ip4;

        string priv_ip = get_nc_item(field_private_ip, network_config);
        m_private_ip = !priv_ip.empty() ? priv_ip : m_ip4;

        string hostname = get_nc_item(field_hostname, network_config);
        m_hostname = !hostname.empty() ? hostname : m_private_ip;

        string access_user = get_nc_item(field_whoami, network_config);
        m_username = !access_user.empty() ? access_user : "vagrant";

        m_homedir = (m_username == "root") ? "/root/" :
            mxb::string_printf("/home/%s/", m_username.c_str());

        m_sudo = envvar_get_set(field_access_sudo.c_str(), " sudo ");
        m_sshkey = get_nc_item(field_keyfile, network_config);

        success = true;
    }

    return success;
}

std::string Nodes::mdbci_node_name(int node)
{
    return m_vms[node].m_name;
}

/**
 * Find variable in the MDBCI network config file.
 *
 * @param item_name Name of the variable
 * @param network_config File contents
 * @return value of variable or empty value if not found
 */
std::string Nodes::VMNode::get_nc_item(const string& item_name, const string& network_config)
{
    size_t start = network_config.find(item_name);
    if (start == std::string::npos)
    {
        return "";
    }

    size_t end = network_config.find("\n", start);
    size_t equal = network_config.find("=", start);
    if (end == std::string::npos)
    {
        end = network_config.length();
    }
    if (equal == std::string::npos)
    {
        return "";
    }

    std::string str = network_config.substr(equal + 1, end - equal - 1);
    str.erase(remove(str.begin(), str.end(), ' '), str.end());

    setenv(item_name.c_str(), str.c_str(), 1);

    return str;
}

int Nodes::get_N()
{
    int n_nodes = 0;
    while (true)
    {
        string item = mxb::string_printf("%s_%03d_network", m_prefix.c_str(), n_nodes);
        if (m_network_config.find(item) != string::npos)
        {
            n_nodes++;
        }
        else
        {
            break;
        }
    }

    // Is this required?
    string env_name = m_prefix + "_N";
    setenv(env_name.c_str(), std::to_string(n_nodes).c_str(), 1);
    return n_nodes;
}

Nodes::SshResult Nodes::VMNode::run_cmd_output(const string& cmd, CmdPriv priv)
{
    bool sudo = (priv == CmdPriv::SUDO);

    string total_cmd;
    total_cmd.reserve(512);
    if (m_type == NodeType::LOCAL)
    {
        // The command can be ran as is.
        if (sudo)
        {
            total_cmd.append(m_sudo).append(" ");
        }
        total_cmd.append(cmd);
    }
    else
    {
        string ssh_cmd_p2 = sudo ? mxb::string_printf("'%s %s'", m_sudo.c_str(), cmd.c_str()) :
            mxb::string_printf("'%s'", cmd.c_str());
        total_cmd.append(m_ssh_cmd_p1).append(" ").append(ssh_cmd_p2);
    }

    Nodes::SshResult rval;
    FILE* pipe = popen(total_cmd.c_str(), "r");
    if (pipe)
    {
        const size_t buflen = 1024;
        string collected_output;
        collected_output.reserve(buflen);   // May end up larger.

        char buffer[buflen];
        while (fgets(buffer, buflen, pipe))
        {
            collected_output.append(buffer);
        }
        mxb::rtrim(collected_output);
        rval.output = std::move(collected_output);

        int exit_code = pclose(pipe);
        rval.rc = (WIFEXITED(exit_code)) ? WEXITSTATUS(exit_code) : 256;
    }
    else
    {
        std::cout << m_name << ": popen() failed when running command " << total_cmd << "\n";
    }
    return rval;
}

void Nodes::VMNode::write_node_env_vars()
{
    auto& name = m_name;
    auto write_env_var = [this](const string& suffix, const string& val) {
            string env_var_name = m_name + suffix;
            setenv(env_var_name.c_str(), val.c_str(), 1);
        };

    write_env_var("_network", m_ip4);
    write_env_var("_network6", m_ip6);
    write_env_var("_private_ip", m_private_ip);
    write_env_var("_hostname", m_hostname);
    write_env_var("_whoami", m_username);
}

Nodes::SshResult Nodes::ssh_output(const std::string& cmd, int node, bool sudo)
{
    return m_vms[node].run_cmd_output(cmd, sudo ? VMNode::CmdPriv::SUDO : VMNode::CmdPriv::NORMAL);
}

const char* Nodes::ip_private(int i) const
{
    return m_vms[i].m_private_ip.c_str();
}

const char* Nodes::ip6(int i) const
{
    return m_vms[i].m_ip6.c_str();
}

const char* Nodes::hostname(int i) const
{
    return m_vms[i].m_hostname.c_str();
}

const char* Nodes::access_user(int i) const
{
    return m_vms[i].m_username.c_str();
}

const char* Nodes::access_homedir(int i) const
{
    return m_vms[i].m_homedir.c_str();
}

const char* Nodes::access_sudo(int i) const
{
    return m_vms[i].m_sudo.c_str();
}

const char* Nodes::sshkey(int i) const
{
    return m_vms[i].m_sshkey.c_str();
}

const std::string& Nodes::prefix() const
{
    return m_prefix;
}

const char* Nodes::ip4(int i) const
{
    return m_vms[i].m_ip4.c_str();
}

bool Nodes::verbose() const
{
    return m_shared.verbose;
}

void Nodes::write_env_vars()
{
    for (auto& vm : m_vms)
    {
        vm.write_node_env_vars();
    }
}
