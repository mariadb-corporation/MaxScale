#include <maxtest/nodes.hh>

#include <algorithm>
#include <cstring>
#include <future>
#include <string>
#include <csignal>
#include <maxbase/format.hh>
#include <maxtest/envv.hh>
#include <maxtest/log.hh>

using std::string;
using std::move;
using CmdPriv = mxt::VMNode::CmdPriv;

namespace
{
// Options given when running ssh from command line. The first line enables connection multiplexing,
// allowing repeated ssh-invocations to use an existing connection.
// Second line disables host ip and key checks.
const char ssh_opts[] = "-o ControlMaster=auto -o ControlPath=./maxscale-test-%r@%h:%p -o ControlPersist=yes "
                        "-o CheckHostIP=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "
                        "-o LogLevel=quiet ";
}

Nodes::Nodes(mxt::SharedData* shared)
    : m_shared(*shared)
{
}

namespace maxtest
{
VMNode::VMNode(SharedData& shared, const string& name)
    : m_name(name)
    , m_shared(shared)
{
}

VMNode::~VMNode()
{
    close_ssh_master();
}
}

namespace maxtest
{
bool VMNode::init_ssh_master()
{
    close_ssh_master();
    bool init_ok = false;
    if (m_ip4 == "127.0.0.1")
    {
        m_type = NodeType::LOCAL;
        init_ok = true;     // No master connection needed for local.
    }
    else
    {
        m_ssh_cmd_p1 = mxb::string_printf("ssh -i %s %s %s@%s",
                                          m_sshkey.c_str(), ssh_opts,
                                          m_username.c_str(), m_ip4.c_str());

        // For initiating the master connection, just part1 is enough.
        FILE* instream = popen(m_ssh_cmd_p1.c_str(), "w");
        if (instream)
        {
            m_ssh_master_pipe = instream;
            init_ok = true;
        }
        else
        {
            log().log_msgf("popen() failed on '%s' when forming master ssh connection.",
                           m_name.c_str());
        }
    }

    // Test the connection. If this doesn't work, continuing is pointless.
    bool rval = false;
    if (init_ok)
    {
        if (run_cmd("ls > /dev/null") == 0)
        {
            rval = true;
        }
        else
        {
            log().log_msgf("SSH/Bash check on '%s' failed.", m_name.c_str());
        }
    }
    return rval;
}

void VMNode::close_ssh_master()
{
    if (m_ssh_master_pipe)
    {
        fprintf(m_ssh_master_pipe, "exit\n");
        pclose(m_ssh_master_pipe);
        m_ssh_master_pipe = nullptr;
    }
}

int VMNode::run_cmd(const std::string& cmd, CmdPriv priv)
{
    string opening_cmd;
    if (m_type == NodeType::LOCAL)
    {
        opening_cmd = "bash";
    }
    else
    {
        opening_cmd = m_ssh_cmd_p1;
        if (!verbose())
        {
            opening_cmd += " > /dev/null";
        }
    }

    // Run in two stages so that "sudo" applies to all commands in the string.
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
        log().add_failure("popen() failed when running command '%s' on %s.",
                          opening_cmd.c_str(), m_name.c_str());
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
        log().log_msgf("Command '%s' failed on %s. Error: %s",
                       cmd.c_str(), m_name.c_str(), mxb_strerror(errno));
        rc = 256;
    }
    return rc;
}

int VMNode::run_cmd_sudo(const string& cmd)
{
    return run_cmd(cmd, CmdPriv::SUDO);
}
}

int Nodes::ssh_node(int node, const string& ssh, bool sudo)
{
    return m_vms[node]->run_cmd(ssh, sudo ? CmdPriv::SUDO : CmdPriv::NORMAL);
}

int Nodes::ssh_node_f(int node, bool sudo, const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    string cmd = mxb::string_vprintf(format, valist);
    va_end(valist);
    return ssh_node(node, cmd, sudo);
}

int Nodes::copy_to_node(int i, const char* src, const char* dest)
{
    if (i >= (int)m_vms.size())
    {
        return 1;
    }
    return m_vms[i]->copy_to_node(src, dest) ? 0 : 1;
}

bool mxt::VMNode::copy_to_node(const string& src, const string& dest)
{
    string cmd;
    if (m_type == NodeType::LOCAL)
    {
        cmd = mxb::string_printf("cp %s %s", src.c_str(), dest.c_str());
    }
    else
    {
        cmd = mxb::string_printf("scp -q -r -i %s %s %s %s@%s:%s",
                                 m_sshkey.c_str(), ssh_opts, src.c_str(),
                                 m_username.c_str(), m_ip4.c_str(), dest.c_str());
    }

    int rc = system(cmd.c_str());
    if (rc != 0)
    {
        log().log_msgf("Copy to VM %s failed. Command '%s' returned %i.",
                       m_name.c_str(), cmd.c_str(), rc);
    }
    return rc == 0;
}

int Nodes::copy_to_node_legacy(const char* src, const char* dest, int i)
{
    return copy_to_node(i, src, dest);
}

int Nodes::copy_from_node(int i, const char* src, const char* dest)
{
    if (i >= (int)m_vms.size())
    {
        return 1;
    }
    return m_vms[i]->copy_from_node(src, dest) ? 0 : 1;
}

bool mxt::VMNode::copy_from_node(const string& src, const string& dest)
{
    string cmd;
    if (m_type == NodeType::LOCAL)
    {
        cmd = mxb::string_printf("cp %s %s", src.c_str(), dest.c_str());
    }
    else
    {
        cmd = mxb::string_printf("scp -q -r -i %s %s %s@%s:%s %s",
                                 m_sshkey.c_str(), ssh_opts, m_username.c_str(), m_ip4.c_str(),
                                 src.c_str(), dest.c_str());
    }

    int rc = system(cmd.c_str());
    if (rc != 0)
    {
        log().log_msgf("Copy from VM %s failed. Command '%s' returned %i.",
                       m_name.c_str(), cmd.c_str(), rc);
    }
    return rc == 0;
}

void Nodes::clear_vms()
{
    m_vms.clear();
    m_vms.reserve(4);
}

bool Nodes::add_node(const mxt::NetworkConfig& nwconfig, const string& name)
{
    bool rval = false;
    auto node = std::make_unique<mxt::VMNode>(m_shared, name);
    if (node->configure(nwconfig))
    {
        m_vms.push_back(move(node));
        rval = true;
    }
    return rval;
}

bool mxt::VMNode::configure(const mxt::NetworkConfig& network_config)
{
    auto& name = m_name;
    string field_network = name + "_network";

    bool success = false;
    string ip4 = get_nc_item(network_config, field_network);
    if (!ip4.empty())
    {
        m_ip4 = ip4;

        string field_network6 = name + "_network6";
        string field_private_ip = name + "_private_ip";
        string field_hostname = name + "_hostname";
        string field_keyfile = name + "_keyfile";
        string field_whoami = name + "_whoami";
        string field_access_sudo = name + "_access_sudo";

        string ip6 = get_nc_item(network_config, field_network6);
        m_ip6 = !ip6.empty() ? ip6 : m_ip4;

        string priv_ip = get_nc_item(network_config, field_private_ip);
        m_private_ip = !priv_ip.empty() ? priv_ip : m_ip4;

        string hostname = get_nc_item(network_config, field_hostname);
        m_hostname = !hostname.empty() ? hostname : m_private_ip;

        string access_user = get_nc_item(network_config, field_whoami);
        m_username = !access_user.empty() ? access_user : "vagrant";

        m_homedir = (m_username == "root") ? "/root/" :
            mxb::string_printf("/home/%s/", m_username.c_str());

        m_sudo = envvar_get_set(field_access_sudo.c_str(), " sudo ");
        m_sshkey = get_nc_item(network_config, field_keyfile);

        success = true;
    }

    return success;
}

std::string Nodes::mdbci_node_name(int node)
{
    return m_vms[node]->m_name;
}

namespace maxtest
{
/**
 * Read key value from MDBCI network config contents.
 *
 * @param nwconfig File contents as a map
 * @param search_key Name of field to read
 * @return value of variable or empty value if not found
 */
std::string VMNode::get_nc_item(const mxt::NetworkConfig& nwconfig, const string& search_key)
{
    string rval;
    auto it = nwconfig.find(search_key);
    if (it != nwconfig.end())
    {
        rval = it->second;
    }

    if (verbose())
    {
        if (rval.empty())
        {
            printf("'%s' not found in network config.\n", search_key.c_str());
        }
        else
        {
            printf("'%s' is '%s'\n", search_key.c_str(), rval.c_str());
        }
    }
    return rval;
}

mxt::CmdResult VMNode::run_cmd_output(const string& cmd, CmdPriv priv)
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

    mxt::CmdResult rval;
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
        log().add_failure("popen() failed when running command '%s' on %s.",
                          total_cmd.c_str(), m_name.c_str());
    }
    return rval;
}

void VMNode::write_node_env_vars()
{
    auto write_env_var = [this](const string& suffix, const string& val) {
            string env_var_name = m_name + suffix;
            setenv(env_var_name.c_str(), val.c_str(), 1);
        };

    write_env_var("_network", m_ip4);
    write_env_var("_network6", m_ip6);
    write_env_var("_private_ip", m_private_ip);
    write_env_var("_hostname", m_hostname);
    write_env_var("_whoami", m_username);
    write_env_var("_keyfile", m_sshkey);
}

const char* VMNode::ip4() const
{
    return m_ip4.c_str();
}

const string& VMNode::ip4s() const
{
    return m_ip4;
}

const string& VMNode::ip6s() const
{
    return m_ip6;
}

const char* VMNode::priv_ip() const
{
    return m_private_ip.c_str();
}

const char* VMNode::hostname() const
{
    return m_hostname.c_str();
}

const char* VMNode::access_user() const
{
    return m_username.c_str();
}

const char* VMNode::access_homedir() const
{
    return m_homedir.c_str();
}

const char* VMNode::access_sudo() const
{
    return m_sudo.c_str();
}

const char* VMNode::sshkey() const
{
    return m_sshkey.c_str();
}

void VMNode::set_local()
{
    m_ip4 = "127.0.0.1";
    m_ip6 = m_ip4;
    m_private_ip = m_ip4;
    m_type = NodeType::LOCAL;
}

TestLogger& VMNode::log()
{
    return m_shared.log;
}

bool VMNode::verbose() const
{
    return m_shared.settings.verbose;
}

bool VMNode::is_remote() const
{
    return m_type == NodeType::REMOTE;
}

mxt::CmdResult VMNode::run_cmd_output_sudo(const string& cmd)
{
    return run_cmd_output(cmd, CmdPriv::SUDO);
}

mxt::CmdResult VMNode::run_sql_query(const std::string& sql)
{
    string cmd = mxb::string_printf("mysql -N -s -e \"%s\"", sql.c_str());
    return run_cmd_output_sudo(cmd);
}

bool VMNode::copy_to_node_sudo(const string& src, const string& dest)
{
    const char err_fmt[] = "Command '%s' failed. Output: %s";
    bool rval = false;
    string temp_file = mxb::string_printf("%s/temporary.tmp", m_homedir.c_str());
    if (copy_to_node(src, temp_file))
    {
        string copy_cmd = mxb::string_printf("cp %s %s", temp_file.c_str(), dest.c_str());
        string rm_cmd = mxb::string_printf("rm %s", temp_file.c_str());
        auto copy_res = run_cmd_output_sudo(copy_cmd);
        auto rm_res = run_cmd_output_sudo(rm_cmd);
        if (copy_res.rc == 0)
        {
            if (rm_res.rc == 0)
            {
                rval = true;
            }
            else
            {
                log().add_failure(err_fmt, rm_cmd.c_str(), rm_res.output.c_str());
            }
        }
        else
        {
            log().add_failure(err_fmt, copy_cmd.c_str(), copy_res.output.c_str());
        }
    }
    return rval;
}
}

mxt::CmdResult Nodes::ssh_output(const std::string& cmd, int node, bool sudo)
{
    return m_vms[node]->run_cmd_output(cmd, sudo ? CmdPriv::SUDO : CmdPriv::NORMAL);
}

const char* Nodes::ip_private(int i) const
{
    return m_vms[i]->priv_ip();
}

const char* Nodes::ip6(int i) const
{
    return m_vms[i]->ip6s().c_str();
}

const char* Nodes::hostname(int i) const
{
    return m_vms[i]->hostname();
}

const char* Nodes::access_user(int i) const
{
    return m_vms[i]->access_user();
}

const char* Nodes::access_homedir(int i) const
{
    return m_vms[i]->access_homedir();
}

const char* Nodes::access_sudo(int i) const
{
    return m_vms[i]->access_sudo();
}

const char* Nodes::sshkey(int i) const
{
    return m_vms[i]->sshkey();
}

const char* Nodes::ip4(int i) const
{
    return m_vms[i]->ip4();
}

bool Nodes::verbose() const
{
    return m_shared.settings.verbose;
}

void Nodes::write_env_vars()
{
    for (auto& vm : m_vms)
    {
        vm->write_node_env_vars();
    }
}

int Nodes::n_nodes() const
{
    return m_vms.size();
}

mxt::VMNode* Nodes::node(int i)
{
    return m_vms[i].get();
}

const mxt::VMNode* Nodes::node(int i) const
{
    return m_vms[i].get();
}
