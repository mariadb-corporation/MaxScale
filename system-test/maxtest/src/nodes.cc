/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/nodes.hh>

#include <algorithm>
#include <cstring>
#include <future>
#include <string>
#include <csignal>
#include <maxbase/format.hh>
#include <maxbase/string.hh>
#include <maxtest/log.hh>
#include "envv.hh"

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

const char err_local_cmd[] = "Attempted to run command '%s' on node %s. Running remote commands is not "
                             "supported in local mode.";
}

Nodes::Nodes(mxt::SharedData* shared)
    : m_shared(*shared)
{
}

namespace maxtest
{
Node::Node(SharedData& shared, string name, string mariadb_executable)
    : m_name(std::move(name))
    , m_shared(shared)
    , m_mariadb_executable(std::move(mariadb_executable))

{
}

VMNode::VMNode(SharedData& shared, string name, string mariadb_executable)
    : Node(shared, std::move(name), std::move(mariadb_executable))
{
}

VMNode::~VMNode()
{
    close_ssh_master();
}

Node::Type VMNode::type() const
{
    return Node::Type::REMOTE;
}

LocalNode::LocalNode(SharedData& shared, std::string name, std::string mariadb_executable)
    : Node(shared, std::move(name), std::move(mariadb_executable))
{
}

Node::Type LocalNode::type() const
{
    return Node::Type::LOCAL;
}

bool LocalNode::configure(const mxb::ini::map_result::ConfigSection& cnf)
{
    return base_configure(cnf);
}

bool LocalNode::init_connection()
{
    return true;
}

int LocalNode::run_cmd(const string& cmd, CmdPriv priv)
{
    int rval = -1;
    // For local nodes, allow non-sudo commands. Hopefully this is enough to prevent most destructive
    // changes.
    if (priv == CmdPriv::NORMAL)
    {
        if (m_shared.run_shell_command(cmd, ""))
        {
            rval = 0;
        }
    }
    else
    {
        string errmsg = mxb::string_printf(err_local_cmd, cmd.c_str(), m_name.c_str());
        log().log_msg(errmsg);
    }
    return rval;
}

mxt::CmdResult LocalNode::run_cmd_output(const string& cmd, CmdPriv priv)
{
    mxt::CmdResult rval;
    // For local nodes, allow non-sudo commands. Hopefully this is enough to prevent most destructive
    // changes.
    if (priv == CmdPriv::NORMAL)
    {
        rval = m_shared.run_shell_cmd_output(cmd);
    }
    else
    {
        string errmsg = mxb::string_printf(err_local_cmd, cmd.c_str(), m_name.c_str());
        log().log_msg(errmsg);
        rval.output = errmsg;
    }
    return rval;
}

bool LocalNode::copy_to_node(const string& src, const string& dest)
{
    log().log_msgf("Tried to copy file '%s' to %s. Copying files is not supported in local mode.",
                   src.c_str(), m_name.c_str());
    return false;
}

bool LocalNode::copy_from_node(const string& src, const string& dest)
{
    log().log_msgf("Tried to copy file '%s' from %s. Copying files is not supported in local mode.",
                   src.c_str(), m_name.c_str());
    return false;
}

bool LocalNode::start_process(std::string_view params)
{
    const string* cmd = &m_start_proc_cmd;
    string tmp;
    if (!params.empty())
    {
        tmp = mxb::string_printf("%s %.*s", m_start_proc_cmd.c_str(), (int)params.size(), params.data());
        cmd = &tmp;
    }
    return system(cmd->c_str()) == 0;
}

bool LocalNode::stop_process()
{
    return system(m_stop_proc_cmd.c_str()) == 0;
}

bool LocalNode::reset_process_datafiles()
{
    return system(m_reset_data_cmd.c_str()) == 0;
}

bool VMNode::init_connection()
{
    close_ssh_master();
    bool init_ok = false;
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

    // Test the connection. If this doesn't work, continuing is pointless.
    bool rval = false;
    if (init_ok)
    {
        if (Node::run_cmd("ls > /dev/null") == 0)
        {
            rval = true;
        }
        else
        {
            log().log_msgf("SSH-check on '%s' failed.", m_name.c_str());
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
    string opening_cmd = m_ssh_cmd_p1;
    if (!verbose())
    {
        opening_cmd += " > /dev/null";
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

int Node::run_cmd(const string& cmd)
{
    return run_cmd(cmd, CmdPriv::NORMAL);
}

int Node::run_cmd_sudo(const string& cmd)
{
    return run_cmd(cmd, CmdPriv::SUDO);
}

mxt::CmdResult Node::run_cmd_output(const string& cmd)
{
    return run_cmd_output(cmd, CmdPriv::NORMAL);
}

bool VMNode::copy_to_node(const string& src, const string& dest)
{
    if (dest == "~" || dest == "~/")
    {
        log().add_failure("Don't rely on tilde expansion in copy_to_node, "
                          "using it will not work if scp uses the SFTP protocol. "
                          "Replace it with the actual path to the file.");
        return false;
    }

    string cmd = mxb::string_printf("scp -q -r -i %s %s %s %s@%s:%s", m_sshkey.c_str(), ssh_opts,
                                    src.c_str(), m_username.c_str(), m_ip4.c_str(), dest.c_str());
    int rc = system(cmd.c_str());
    if (rc != 0)
    {
        log().log_msgf("Copy to VM %s failed. Command '%s' returned %i.",
                       m_name.c_str(), cmd.c_str(), rc);
    }
    return rc == 0;
}

std::unique_ptr<mxt::Node> create_node(const mxb::ini::map_result::Configuration::value_type& config,
                                       mxt::SharedData& shared)
{
    std::unique_ptr<mxt::Node> rval;
    const string& header = config.first;
    auto& log = shared.log;
    const string key_loc = "location";
    const char missing[] = "Section '%s' is missing mandatory parameter '%s'.";
    const auto& kvs = config.second.key_values;

    auto it = kvs.find(key_loc);
    if (it != kvs.end())
    {
        std::unique_ptr<mxt::Node> new_node;
        const string& val_loc = it->second.value;

        if (val_loc == "local")
        {
            new_node = std::make_unique<mxt::LocalNode>(shared, header, "mariadb");
        }
        else if (val_loc == "docker")
        {
            new_node = std::make_unique<mxt::DockerNode>(shared, header, "mariadb");
            shared.using_docker = true;
        }
        else if (val_loc == "remote")
        {
            log.add_failure("'remote' node location not supported yet.");
        }
        else
        {
            log.add_failure("Unrecognized node location. Use 'local', 'docker' or 'remote'.");
        }

        if (new_node)
        {
            if (new_node->configure(config.second))
            {
                rval = std::move(new_node);
            }
            else
            {
                log.add_failure("Configuration of '%s' failed.", header.c_str());
            }
        }
    }
    else
    {
        log.add_failure(missing, header.c_str(), key_loc.c_str());
    }
    return rval;
}

DockerNode::DockerNode(SharedData& shared, std::string name, std::string mariadb_executable)
    : Node(shared, std::move(name), std::move(mariadb_executable))
{
}

Node::Type DockerNode::type() const
{
    return Node::Type::DOCKER;
}

bool DockerNode::configure(const mxb::ini::map_result::ConfigSection& cnf)
{
    bool rval = false;
    if (base_configure(cnf))
    {
        auto& s = m_shared;
        rval = s.read_str(cnf, "container", m_container) && s.read_str(cnf, "image", m_image)
            && s.read_str(cnf, "volume", m_volume) && s.read_str(cnf, "volume_dest", m_volume_dest);
    }
    return rval;
}

bool DockerNode::init_connection()
{
    bool node_exists = false;
    bool node_running = false;
    mxb::Json info;

    // First check if container is running. If not, start it.
    auto check_container_status = [&]() {
        info = m_shared.get_container_info(m_container);
        if (info)
        {
            node_exists = true;
            node_running = info.get_string("State") == "running";
        }
        else
        {
            node_exists = false;
            node_running = false;
        }
    };

    check_container_status();

    if (!node_exists || !node_running)
    {
        if (node_exists && !node_running)
        {
            m_shared.run_shell_cmd_outputf("docker rm -fv %s", m_container.c_str());
            m_shared.run_shell_cmd_outputf("docker volume rm %s", m_volume.c_str());
        }

        string start_cmd = mxb::string_printf(
            "docker run -d --rm --mount type=volume,source=%s,destination=%s --name %s %s",
            m_volume.c_str(), m_volume_dest.c_str(), m_container.c_str(), m_image.c_str());

        auto start_res = m_shared.run_shell_cmd_output(start_cmd);
        if (start_res.rc == 0)
        {
            // Container should be running, update data.
            m_shared.update_docker_container_info();
            check_container_status();
            if (node_running)
            {
                // If container was just started, start the server process so it runs initializations.
                start_process("");
            }
            else
            {
                log().add_failure("Container not running even though start command completed.");
            }
        }
        else
        {
            log().add_failure("Failed to start container. Command '%s' failed. Error %i: %s",
                              start_cmd.c_str(), start_res.rc, start_res.output.c_str());
        }
    }

    bool container_ok = false;
    if (node_running)
    {
        // Container is running. The ip of the container is assigned by Docker in the bridge
        // network. Fetch it and save it.
        mxb::Json network_info = info.at("NetworkSettings/Networks/bridge");
        if (network_info)
        {
            string ip4 = network_info.get_string("IPAddress");
            string ip6 = network_info.get_string("GlobalIPv6Address");
            if (!ip4.empty() && !ip6.empty())
            {
                if (ip4 != m_ip4)
                {
                    log().log_msgf("Overwriting %s IPv4 address: %s --> %s",
                                   m_container.c_str(), m_ip4.c_str(), ip4.c_str());
                    m_ip4 = std::move(ip4);
                    m_private_ip = m_ip4;
                }
                if (ip6 != m_ip6)
                {
                    log().log_msgf("Overwriting %s IPv6 address: %s --> %s",
                                   m_container.c_str(), m_ip6.c_str(), ip6.c_str());
                    m_ip6 = std::move(ip6);
                }
                container_ok = true;
            }
            else
            {
                log().add_failure("No IP addresses in container %s network info.", m_container.c_str());
            }
        }
        else
        {
            log().add_failure("No network info from container %s.", m_container.c_str());
        }
    }
    return container_ok;
}

int DockerNode::run_cmd(const string& cmd, CmdPriv priv)
{
    return 1;
}

mxt::CmdResult DockerNode::run_cmd_output(const string& cmd, CmdPriv priv)
{
    // Docker exec always runs as sudo inside the container.
    string docker_cmd = mxb::string_printf("docker exec %s %s", m_container.c_str(), cmd.c_str());
    return m_shared.run_shell_cmd_output(docker_cmd);
}

bool DockerNode::copy_to_node(const string& src, const string& dest)
{
    bool rval = false;
    string cmd = mxb::string_printf("docker cp %s %s:%s", src.c_str(), m_container.c_str(), dest.c_str());
    auto res = m_shared.run_shell_cmd_output(cmd);
    if (res.rc == 0)
    {
        rval = true;
    }
    else
    {
        log().add_failure("Copy to container %s failed. Error %i: %s",
                          m_container.c_str(), res.rc, res.output.c_str());
    }
    return rval;
}

bool DockerNode::copy_from_node(const string& src, const string& dest)
{
    bool rval = false;
    string cmd = mxb::string_printf("docker cp %s:%s %s", m_container.c_str(), src.c_str(), dest.c_str());
    auto res = m_shared.run_shell_cmd_output(cmd);
    if (res.rc == 0)
    {
        rval = true;
    }
    else
    {
        log().add_failure("Copy from container %s failed. Error %i: %s",
                          m_container.c_str(), res.rc, res.output.c_str());
    }
    return rval;
}

bool DockerNode::start_process(std::string_view params)
{
    const string* cmd = &m_start_proc_cmd;
    string tmp;
    if (!params.empty())
    {
        tmp = mxb::string_printf("%s %.*s", m_start_proc_cmd.c_str(), (int)params.size(), params.data());
        cmd = &tmp;
    }
    return exec_cmd(*cmd);
}

bool DockerNode::stop_process()
{
    return exec_cmd(m_stop_proc_cmd);
}

bool DockerNode::reset_process_datafiles()
{
    return exec_cmd(m_reset_data_cmd);
}

/**
 * Run a command, expecting success.
 */
bool DockerNode::exec_cmd(const string& cmd)
{
    auto res = run_cmd_output(cmd, CmdPriv::SUDO);
    bool rval = false;
    if (res.rc == 0)
    {
        rval = true;
    }
    else
    {
        log().add_failure("Command '%s' in container %s failed. Error %i: '%s'",
                          cmd.c_str(), m_container.c_str(), res.rc, res.output.c_str());
    }
    return rval;
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
    string cmd = mxb::string_printf("scp -q -r -i %s %s %s@%s:%s %s",
                                    m_sshkey.c_str(), ssh_opts, m_username.c_str(), m_ip4.c_str(),
                                    src.c_str(), dest.c_str());
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
    auto node = std::make_unique<mxt::VMNode>(m_shared, name, mariadb_executable());
    if (node->configure(nwconfig))
    {
        m_vms.push_back(move(node));
        rval = true;
    }
    return rval;
}

void Nodes::add_node(std::unique_ptr<mxt::Node> node)
{
    m_vms.push_back(std::move(node));
}

bool mxt::VMNode::configure(const mxt::NetworkConfig& network_config)
{
    auto& name = m_name;
    string field_network = name + "_network";

    bool success = false;
    string ip4 = m_shared.get_nc_item(network_config, field_network);
    if (!ip4.empty())
    {
        m_ip4 = ip4;

        string field_network6 = name + "_network6";
        string field_private_ip = name + "_private_ip";
        string field_hostname = name + "_hostname";
        string field_keyfile = name + "_keyfile";
        string field_whoami = name + "_whoami";
        string field_access_sudo = name + "_access_sudo";

        string ip6 = m_shared.get_nc_item(network_config, field_network6);
        m_ip6 = !ip6.empty() ? ip6 : m_ip4;

        string priv_ip = m_shared.get_nc_item(network_config, field_private_ip);
        m_private_ip = !priv_ip.empty() ? priv_ip : m_ip4;

        string hostname = m_shared.get_nc_item(network_config, field_hostname);
        m_hostname = !hostname.empty() ? hostname : m_private_ip;

        string access_user = m_shared.get_nc_item(network_config, field_whoami);
        m_username = !access_user.empty() ? access_user : "vagrant";

        m_homedir = (m_username == "root") ? "/root/" :
            mxb::string_printf("/home/%s/", m_username.c_str());

        m_sudo = envvar_get_set(field_access_sudo.c_str(), " sudo ");
        m_sshkey = m_shared.get_nc_item(network_config, field_keyfile);

        success = true;
    }

    return success;
}

bool mxt::VMNode::configure(const mxb::ini::map_result::ConfigSection& cnf)
{
    bool rval = false;
    if (base_configure(cnf))
    {
        auto& s = m_shared;
        rval = s.read_str(cnf, "ip6", m_ip6) && s.read_str(cnf, "ip_priv", m_private_ip)
            && s.read_str(cnf, "ssh_username", m_username) && s.read_str(cnf, "ssh_keyfile", m_sshkey)
            && s.read_str(cnf, "sudo", m_sudo);
    }
    return rval;
}

std::string Nodes::mdbci_node_name(int node)
{
    return m_vms[node]->m_name;
}

namespace maxtest
{

mxt::CmdResult VMNode::run_cmd_output(const string& cmd, CmdPriv priv)
{
    bool sudo = (priv == CmdPriv::SUDO);

    string ssh_cmd_p2 = sudo ? mxb::string_printf("'%s %s'", m_sudo.c_str(), cmd.c_str()) :
        mxb::string_printf("'%s'", cmd.c_str());
    string total_cmd;
    total_cmd.reserve(512);
    total_cmd.append(m_ssh_cmd_p1).append(" ").append(ssh_cmd_p2);

    return m_shared.run_shell_cmd_output(total_cmd);
}

bool VMNode::start_process(std::string_view params)
{
    const string* cmd = &m_start_proc_cmd;
    string tmp;
    if (!params.empty())
    {
        tmp = mxb::string_printf("%s %.*s", m_start_proc_cmd.c_str(), (int)params.size(), params.data());
        cmd = &tmp;
    }
    return run_cmd_sudo(*cmd) == 0;
}

bool VMNode::stop_process()
{
    return run_cmd_sudo(m_stop_proc_cmd) == 0;
}

bool VMNode::reset_process_datafiles()
{
    return run_cmd_sudo(m_reset_data_cmd) == 0;
}

void Node::write_node_env_vars()
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

const char* Node::name() const
{
    return m_name.c_str();
}

const char* Node::ip4() const
{
    return m_ip4.c_str();
}

const string& Node::ip4s() const
{
    return m_ip4;
}

const string& Node::ip6s() const
{
    return m_ip6;
}

const char* Node::priv_ip() const
{
    return m_private_ip.c_str();
}

const char* Node::hostname() const
{
    return m_hostname.c_str();
}

const char* Node::access_user() const
{
    return m_username.c_str();
}

const char* Node::access_homedir() const
{
    return m_homedir.c_str();
}

const char* Node::access_sudo() const
{
    return m_sudo.c_str();
}

const char* Node::sshkey() const
{
    return m_sshkey.c_str();
}

TestLogger& Node::log()
{
    return m_shared.log;
}

bool Node::verbose() const
{
    return m_shared.settings.verbose;
}

mxt::CmdResult Node::run_cmd_output_sudo(const string& cmd)
{
    return run_cmd_output(cmd, CmdPriv::SUDO);
}

mxt::CmdResult Node::run_sql_query(const std::string& sql)
{
    string cmd = mxb::string_printf("%s -N -s -e \"%s\"", m_mariadb_executable.c_str(), sql.c_str());
    return run_cmd_output_sudo(cmd);
}

bool Node::copy_to_node_sudo(const string& src, const string& dest)
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

void Node::add_linux_user(const string& uname, const string& pw)
{
    auto unamec = uname.c_str();
    string add_user_cmd = mxb::string_printf("useradd %s", unamec);

    auto ret1 = run_cmd_output_sudo(add_user_cmd);
    if (ret1.rc == 0)
    {
        int ret2 = -1;
        if (pw.empty())
        {
            string remove_pw_cmd = mxb::string_printf("passwd --delete %s", unamec);
            ret2 = run_cmd_output_sudof("passwd --delete %s", unamec).rc;
        }
        else
        {
            string add_pw_cmd = mxb::string_printf("echo %s | passwd --stdin %s", pw.c_str(), unamec);
            ret2 = run_cmd_sudo(add_pw_cmd);
        }
        log().expect(ret2 == 0, "Failed to change password of user '%s' on %s: %d",
                     unamec, name(), ret2);
    }
    else
    {
        log().add_failure("Failed to add user '%s' to %s: %s", uname.c_str(), name(), ret1.output.c_str());
    }
}

void Node::remove_linux_user(const string& uname)
{
    string remove_cmd = mxb::string_printf("userdel --remove %s", uname.c_str());
    auto res = run_cmd_output_sudo(remove_cmd);
    log().expect(res.rc == 0, "Failed to remove user '%s' from %s: %s",
                 uname.c_str(), name(), res.output.c_str());
}

void Node::delete_from_node(const string& filepath)
{
    string rm_cmd = mxb::string_printf("rm -f %s", filepath.c_str());
    auto res = run_cmd_output_sudo(rm_cmd);
    log().expect(res.rc == 0, "Failed to delete file '%s' on %s: %s",
                 filepath.c_str(), name(), res.output.c_str());
}

mxt::CmdResult Node::run_cmd_output_sudof(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    string cmd = mxb::string_vprintf(format, args);
    va_end(args);
    return run_cmd_output_sudo(cmd);
}

void Node::add_linux_group(const string& grp_name, const std::vector<std::string>& members)
{
    auto res = run_cmd_output_sudof("groupadd %s", grp_name.c_str());
    if (res.rc == 0)
    {
        for (const auto& mem : members)
        {
            res = run_cmd_output_sudof("groupmems -a %s -g %s", mem.c_str(), grp_name.c_str());
            log().expect(res.rc == 0, "Failed to add user to group: %s", res.output.c_str());
        }
    }
    else
    {
        log().add_failure("Failed to add group '%s' to %s: %s", grp_name.c_str(), name(), res.output.c_str());
    }
}

void Node::remove_linux_group(const std::string& grp_name)
{
    auto res = run_cmd_output_sudof("groupdel %s", grp_name.c_str());
    log().expect(res.rc == 0, "Group delete failed: %s", res.output.c_str());
}

bool Node::base_configure(const mxb::ini::map_result::ConfigSection& cnf)
{
    bool rval = false;
    auto& s = m_shared;
    if (s.read_str(cnf, "ip4", m_ip4) && s.read_str(cnf, "hostname", m_hostname)
        && s.read_str(cnf, "start_cmd", m_start_proc_cmd)
        && s.read_str(cnf, "stop_cmd", m_stop_proc_cmd)
        && s.read_str(cnf, "reset_cmd", m_reset_data_cmd)
        && s.read_str(cnf, "homedir", m_homedir))
    {
        auto& kvs = cnf.key_values;
        auto it = kvs.find("private_ip");
        m_private_ip = (it == kvs.end()) ? m_ip4 : it->second.value;
        rval = true;
    }
    return rval;
}

void Node::set_start_stop_reset_cmds(string&& start, string&& stop, string&& reset)
{
    m_start_proc_cmd = std::move(start);
    m_stop_proc_cmd = std::move(stop);
    m_reset_data_cmd = std::move(reset);
}

bool Node::is_remote() const
{
    // Docker nodes are considered remote in the sense that most sudo-level commands can be run on them.
    // iptables is an exception and must be handled in another way.
    auto t = type();
    return t == Type::REMOTE || t == Type::DOCKER;
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

mxt::Node* Nodes::node(int i)
{
    return m_vms[i].get();
}

const mxt::Node* Nodes::node(int i) const
{
    return m_vms[i].get();
}
