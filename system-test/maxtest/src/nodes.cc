#include <maxtest/nodes.hh>

#include <algorithm>
#include <sstream>
#include <cstring>
#include <future>
#include <functional>
#include <iostream>
#include <string>
#include <csignal>
#include <maxtest/envv.hh>
#include <maxbase/format.hh>

using std::string;

Nodes::Nodes(const char* pref,
             const std::string& network_config,
             bool verbose)
    : verbose(verbose)
    , network_config(network_config)
{
    strcpy(this->prefix, pref);
}

Nodes::~Nodes()
{
    for (auto a : m_ssh_connections)
    {
        pclose(a);
    }
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

string Nodes::generate_ssh_cmd(int node, const string& cmd, bool sudo)
{
    string rval;
    if (strcmp(IP[node], "127.0.0.1") == 0)
    {
        // If node is the local machine, run command as is.
        rval = sudo ? ((string)(access_sudo[node]) + " " + cmd) : cmd;
    }
    else
    {
        // Run command through ssh. The ControlMaster-option enables use of existing pooled connections,
        // greatly speeding up the operation.
        string p1 = mxb::string_printf("ssh -i %s ", sshkey[node]);
        string p2 = "-o UserKnownHostsFile=/dev/null "
                    "-o CheckHostIP=no "
                    "-o ControlMaster=auto "
                    "-o ControlPath=./maxscale-test-%r@%h:%p "
                    "-o ControlPersist=yes "
                    "-o StrictHostKeyChecking=no "
                    "-o LogLevel=quiet ";

        string p3 = mxb::string_printf("%s@%s ", m_access_user[node].c_str(), IP[node]);
        string p4 = sudo ? mxb::string_printf("'%s %s'", access_sudo[node], cmd.c_str()) :
            mxb::string_printf("'%s'", cmd.c_str());
        rval = p1 + p2 + p3 + p4;
    }
    return rval;
}

FILE* Nodes::open_ssh_connection(int node)
{
    std::ostringstream ss;

    if (strcmp(IP[node], "127.0.0.1") == 0)
    {
        ss << "bash";
    }
    else
    {
        ss << "ssh -i " << sshkey[node] << " "
           << "-o UserKnownHostsFile=/dev/null "
           << "-o StrictHostKeyChecking=no "
           << "-o LogLevel=quiet "
           << "-o CheckHostIP=no "
           << "-o ControlMaster=auto "
           << "-o ControlPath=./maxscale-test-%r@%h:%p "
           << "-o ControlPersist=yes "
           << m_access_user[node] << "@"
           << IP[node]
           << (verbose ? "" :  " > /dev/null");
    }

    return popen(ss.str().c_str(), "w");
}

int Nodes::ssh_node(int node, const char* ssh, bool sudo)
{
    if (verbose)
    {
        std::cout << ssh << std::endl;
    }

    int rc = 1;
    FILE* in = open_ssh_connection(node);

    if (in)
    {
        if (sudo)
        {
            fprintf(in, "sudo su -\n");
            fprintf(in, "cd /home/%s\n", m_access_user[node].c_str());
        }

        fprintf(in, "%s\n", ssh);
        rc = pclose(in);
    }

    if (WIFEXITED(rc))
    {
        return WEXITSTATUS(rc);
    }
    else if (WIFSIGNALED(rc) && WTERMSIG(rc) == SIGHUP)
    {
        // SIGHUP appears to happen for SSH connections
        return 0;
    }
    else
    {
        std::cout << strerror(errno) << std::endl;
        return 256;
    }
}

void Nodes::init_ssh_masters()
{
    std::vector<std::thread> threads;
    m_ssh_connections.resize(N);

    for (int i = 0; i < N; i++)
    {
        threads.emplace_back(
            [this, i]() {
                m_ssh_connections[i] = open_ssh_connection(i);
            });
    }

    for (auto& a : threads)
    {
        a.join();
    }
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

    if (strcmp(IP[i], "127.0.0.1") == 0)
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
                sshkey[i],
                src,
                m_access_user[i].c_str(),
                IP[i],
                dest);
    }
    if (verbose)
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
    if (strcmp(IP[i], "127.0.0.1") == 0)
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
                sshkey[i],
                m_access_user[i].c_str(),
                IP[i],
                src,
                dest);
    }
    if (verbose)
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
    char env_name[64];
    N = get_N();

    if ((N > 0) && (N < 255))
    {
        for (int i = 0; i < N; i++)
        {
            // reading IPs
            sprintf(env_name, "%s_%03d_network", prefix, i);
            IP[i] = strdup(get_nc_item(env_name).c_str());

            // reading private IPs
            sprintf(env_name, "%s_%03d_private_ip", prefix, i);
            auto& priv_ip = m_ip_private[i];
            priv_ip = get_nc_item(env_name);
            if (priv_ip.empty())
            {
                priv_ip = IP[i];
            }
            setenv(env_name, priv_ip.c_str(), 1);

            // reading IPv6
            sprintf(env_name, "%s_%03d_network6", prefix, i);
            auto& ip6 = m_ip6[i];
            ip6 = get_nc_item(env_name);
            if (ip6.empty())
            {
                ip6 = IP[i];
            }
            setenv(env_name, ip6.c_str(), 1);

            //reading sshkey
            sprintf(env_name, "%s_%03d_keyfile", prefix, i);
            sshkey[i] = strdup(get_nc_item(env_name).c_str());


            sprintf(env_name, "%s_%03d_whoami", prefix, i);
            auto& access_user = m_access_user[i];
            access_user = get_nc_item(env_name);
            if (access_user.empty())
            {
                access_user = "vagrant";
            }
            setenv(env_name, access_user.c_str(), 1);

            sprintf(env_name, "%s_%03d_access_sudo", prefix, i);
            access_sudo[i] = readenv(env_name, " sudo ");

            if (access_user == "root")
            {
                access_homedir[i] = (char *) "/root/";
            }
            else
            {
                access_homedir[i] = (char *) malloc(access_user.length() + 9);
                sprintf(access_homedir[i], "/home/%s/", access_user.c_str());
            }

            sprintf(env_name, "%s_%03d_hostname", prefix, i);
            auto& hostname = m_hostname[i];
            hostname = get_nc_item(env_name);
            if (hostname.empty())
            {
                hostname = m_ip_private[i];
            }
            setenv(env_name, hostname.c_str(), 1);

            sprintf(env_name, "%s_%03d_start_vm_command", prefix, i);
            string start_vm_def = mxb::string_printf("curr_dir=`pwd`; "
                                                     "cd %s/%s;vagrant resume %s_%03d ; "
                                                     "cd $curr_dir",
                                                     getenv("MDBCI_VM_PATH"), getenv("name"), prefix, i);
            m_start_vm_command[i] = envvar_get_set(env_name, "%s", start_vm_def.c_str());
            setenv(env_name, m_start_vm_command[i].c_str(), 1);

            sprintf(env_name, "%s_%03d_stop_vm_command", prefix, i);
            string stop_vm_def = mxb::string_printf("curr_dir=`pwd`; "
                                                    "cd %s/%s;vagrant suspend %s_%03d ; "
                                                    "cd $curr_dir",
                                                    getenv("MDBCI_VM_PATH"), getenv("name"), prefix, i);
            m_stop_vm_command[i] = envvar_get_set(env_name, "%s", stop_vm_def.c_str());
            setenv(env_name, m_stop_vm_command[i].c_str(), 1);
        }
    }

    return 0;
}

const char* Nodes::ip(int i) const
{
    return use_ipv6 ? m_ip6[i].c_str() : IP[i];
}

std::string Nodes::get_nc_item(const char* item_name)
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

    setenv(item_name, str.c_str(), 1);

    return str;
}

int Nodes::get_N()
{
    int N = 0;
    char item[strlen(prefix) + 13];
    do
    {
        sprintf(item, "%s_%03d_network", prefix, N);
        N++;
    }
    while (network_config.find(item) != std::string::npos);
    sprintf(item, "%s_N", prefix);
    setenv(item, std::to_string(N).c_str(), 1);
    return N - 1 ;
}

int Nodes::start_vm(int node)
{
    return (system(m_start_vm_command[node].c_str()));
}

int Nodes::stop_vm(int node)
{
    return (system(m_stop_vm_command[node].c_str()));
}

Nodes::SshResult Nodes::ssh_output(const std::string& cmd, int node, bool sudo)
{
    Nodes::SshResult rval;
    string ssh_cmd = generate_ssh_cmd(node, cmd, sudo);
    FILE* output_pipe = popen(ssh_cmd.c_str(), "r");
    if (!output_pipe)
    {
        printf("Error opening ssh %s\n", strerror(errno));
        return rval;
    }

    const size_t buflen = 1024;
    string collected_output;
    collected_output.reserve(buflen);   // May end up larger.

    char buffer[buflen];
    while (fgets(buffer, buflen, output_pipe))
    {
        collected_output.append(buffer);
    }
    mxb::rtrim(collected_output);
    rval.output = std::move(collected_output);

    int exit_code = pclose(output_pipe);
    rval.rc = (WIFEXITED(exit_code)) ? WEXITSTATUS(exit_code) : 256;
    return rval;
}

bool Nodes::using_ipv6() const
{
    return use_ipv6;
}

const char* Nodes::ip_private(int i) const
{
    return m_ip_private[i].c_str();
}

const char* Nodes::ip6(int i) const
{
    return m_ip6[i].c_str();
}

const char* Nodes::hostname(int i) const
{
    return m_hostname[i].c_str();
}

const char* Nodes::access_user(int i) const
{
    return m_access_user[i].c_str();
}
