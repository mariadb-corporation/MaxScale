#ifndef MARIADB_NODES_H
#define MARIADB_NODES_H

/**
 * @file mariadb_nodes.h - backend nodes routines
 *
 * @verbatim
 * Revision History
 *
 * Date     Who     Description
 * 17/11/14 Timofey Turenko Initial implementation
 *
 * @endverbatim
 */


#include "mariadb_func.h"
#include <errno.h>
#include <string>

/**
 * @brief A class to handle backend nodes
 * Contains references up to 256 nodes, info about IP, port, ssh key, use name and password for each node
 * Node parameters should be defined in the enviromental variables in the follwing way:
 * prefix_N - N number of nodes in the setup
 * prefix_NNN - IP adress of the node (NNN 3 digits node index)
 * prefix_port_NNN - MariaDB port number of the node
 * prefix_User - User name to access backend setup (should have full access to 'test' DB with GRANT OPTION)
 * prefix_Password - Password to access backend setup
 */
class Mariadb_nodes
{
public:
    /**
     * @brief Constructor
     * @param pref  name of backend setup (like 'repl' or 'galera')
     */
    Mariadb_nodes(const char *pref, const char *test_cwd, bool verbose);

    ~Mariadb_nodes();

    /**
    * @brief  MYSQL structs for every backend node
    */
    MYSQL *nodes[256];
    /**
     * @brief  IP address strings for every backend node
     */
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
     * @brief  MariaDB port for every backend node
     */
    int port[256];
    /**
     * @brief Unix socket to connecto to MariaDB
     */
    char socket[256][1024];
    /**
     * @brief 'socket=$socket' line
     */
    char socket_cmd[256][1024];
    /**
     * @brief  Path to ssh key for every backend node
     */
    char sshkey[256][4096];
    /**
     * @brief Number of backend nodes
     */
    int N;
    /**
     * @brief   User name to access backend nodes
     */
    char user_name[256];
    /**
     * @brief   Password to access backend nodes
     */
    char password[256];
    /**
     * @brief master index of node which was last configured to be Master
     */
    int master;
    /**
     * @brief     name of backend setup (like 'repl' or 'galera')
     */
    char prefix[16];
    /**
     * @brief start_db_command Command to start DB server
     */
    char start_db_command[256][4096];

    /**
     * @brief stop_db_command Command to start DB server
     */
    char stop_db_command[256][4096];

    /**
     * @brief cleanup_db_command Command to remove all
     * data files and re-install DB with mysql_install_db
     */
    char cleanup_db_command[256][4096];

    /**
     * @brief ssl if true ssl will be used
     */
    int ssl;

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

    /**
     * @brief no_set_pos if set to true setup_binlog function do not set log position
     */
    bool no_set_pos;

    /**
     * @brief Verbose command output
     */
    bool verbose;

    /**
     * @brief version Value of @@version
     */
    char version[256][256];

    /**
     * @brief version major part of number value of @@version
     */
    char version_major[256][256];

    /**
     * @brief version Number part of @@version
     */
    char version_number[256][256];

    /**
     * @brief connect open connections to all nodes
     * @return 0  in case of success
     */

    /**
    * @brief v51 true indicates that one backed is 5.1
    */
    bool v51;

    /**
    * @brief test_dir path to test application
    */
    char test_dir[4096];


    /**
     * @brief List of blocked nodes
     */
    bool blocked[256];

    /**
    * @brief  Open connctions to all backend nodes (to 'test' DB)
    * @return 0 in case of success
    */

    /**
     * @brief make_snapshot_command Command line to create a snapshot of all VMs
     */
    char * take_snapshot_command;

    /**
     * @brief revert_snapshot_command Command line to revert a snapshot of all VMs
     */
    char * revert_snapshot_command;

    int connect(int i);
    int connect();

    /**
     * @brief Close connections opened by connect()
     *
     * This sets the values of used @c nodes to NULL.
     */
    void close_connections();

    /**
     * @brief reads IP, Ports, sshkeys for every node from enviromental variables as well as number of nodes (N) and  User/Password
     * @return 0
     */
    int read_env();
    /**
     * @brief  prints all nodes information
     * @return 0
     */
    int print_env();

    /**
     * @brief find_master Tries to find Master node
     * @return Index of Master node
     */
    int find_master();
    /**
     * @brief change_master set a new master node for Master/Slave setup
     * @param NewMaster index of new Master node
     * @param OldMaster index of current Master node
     * @return  0 in case of success
     */
    int change_master(int NewMaster, int OldMaster);

    /**
     * @brief stop_nodes stops mysqld on all nodes
     * @return  0 in case of success
     */
    int stop_nodes();

    /**
     * @brief stop_slaves isues 'stop slave;' to all nodes
     * @return  0 in case of success
     */
    int stop_slaves();

    /**
     * @brief cleanup_db_node Removes all data files and reinstall DB
     * with mysql_install_db
     * @param node
     * @return 0 in case of success
     */
    int cleanup_db_node(int node);

    /**
     * @brief cleanup_db_node Removes all data files and reinstall DB
     * with mysql_install_db for all nodes
     * @param node
     * @return 0 in case of success
     */
    int cleanup_db_nodes();

    /**
     * @brief configures nodes and starts Master/Slave replication
     * @return  0 in case of success
     */
    virtual int start_replication();

    /**
     * @brif BlockNode setup firewall on a backend node to block MariaDB port
     * @param node Index of node to block
     * @return 0 in case of success
     */
    int block_node(int node);

    /**
     * @brief UnblockNode setup firewall on a backend node to unblock MariaDB port
     * @param node Index of node to unblock
     * @return 0 in case of success
     */
    int unblock_node(int node);


    /**
     * @brief Unblock all nodes for this cluster
     * @return 0 in case of success
     */
    int unblock_all_nodes();

    /**
     * @brief clean_iptables removes all itables rules connected to MariaDB port to avoid duplicates
     * @param node Index of node to clean
     * @return 0 in case of success
     */
    int clean_iptables(int node);

    /**
     * @brief Stop DB server on the node
     * @param node Node index
     * @return 0 if success
     */
    int stop_node(int node);

    /**
     * @brief Start DB server on the node
     * @param node Node index
     * @param param command line parameters for DB server start command
     * @return 0 if success
     */
    int start_node(int node, const char* param = "");

    /**
     * @brief Check node via ssh and restart it if it is not resposible
     * @param node Node index
     * @return 0 if node is ok, 1 if start failed
     */
    int check_nodes();

    /**
     * @brief Check if all slaves have "Slave_IO_Running" set to "Yes" and master has N-1 slaves
     * @param master Index of master node
     * @return 0 if everything is ok
     */
    virtual int check_replication();

    /**
     * @brief executes 'CHANGE MASTER TO ..' and 'START SLAVE'
     * @param MYSQL conn struct of slave node
     * @param master_host IP address of master node
     * @param master_port port of master node
     * @param log_file name of log file
     * @param log_pos initial position
     * @return 0 if everything is ok
     */
    int set_slave(MYSQL * conn, char master_host[], int master_port, char log_file[], char log_pos[]);

    /**
     * @brief Creates 'repl' user on all nodes
     * @return 0 if everything is ok
     */
    int set_repl_user();

    /**
     * @brief Get the server_id of the node
     * @param index The index of the node whose server_id to retrieve
     * @return Node id of the server or -1 on error
     */
    int get_server_id(int index);
    std::string get_server_id_str(int index);

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
    char *ssh_node_output(int node, const char *ssh, bool sudo, int *exit_code);

    /**
     * @brief executes shell command on the node using ssh
     * @param index number of the node (index)
     * @param sudo if true the command is executed with root privelegues
     * @param ssh command to execute
     * @return exit code of the command
     */
    int ssh_node(int node, bool sudo, const char *ssh, ...);

    /**
     * @brief Execute 'mysqladmin flush-hosts' on all nodes
     * @return 0 in case of success
     */
    int flush_hosts();

    /**
     * @brief Execute query on all nodes
     * @param sql query to execute
     * @return 0 in case of success
     */
    int execute_query_all_nodes(const char* sql);

    /**
     * @brief execute 'SELECT @@version' against one node and store result in 'version' field
     * @param i Node index
     * @return 0 in case of success
     */
    int get_version(int i);

    /**
     * @brief execute 'SELECT @@version' against all nodes and store result in 'version' field
     * @return 0 in case of success
     */
    int get_versions();


    /**
     * @brief Return lowest server version in the cluster
     * @return The version string of the server with the lowest version number
     */
    std::string get_lowest_version();

    /**
     * @brief truncate_mariadb_logs clean ups MariaDB logs on backend nodes
     * @return 0 if success
     */
    int truncate_mariadb_logs();

    /**
     * @brief configure_ssl Modifies my.cnf in order to enable ssl, redefine access user to require ssl
     * @return 0 if success
     */
    int configure_ssl(bool require);

    /**
     * @brief disable_ssl Modifies my.cnf in order to get rid of ssl, redefine access user to allow connections without ssl
     * @return 0 if success
     */
    int disable_ssl();

    /**
     * @brief Copy a local file to the Node i machine
     * @param src Source file on the local filesystem
     * @param dest Destination file on the remote file system
     * @param i Node index
     * @return exit code of the system command or 1 in case of i > N
     */
    int copy_to_node(const char* src, const char* dest, int i);

    /**
     * @brief Copy a local file to the Node i machine
     * @param src Source file on the remote filesystem
     * @param dest Destination file on the local file system
     * @param i Node index
     * @return exit code of the system command or 1 in case of i > N
     */
    int copy_from_node(const char* src, const char* dest, int i);

    /**
     * @brief Synchronize slaves with the master
     *
     * Only works with master-slave replication and should not be used with Galera clusters.
     * The function expects that the first node, @c nodes[0], is the master.
     */
    void sync_slaves(int node = 0);

    /**
     * @brief Close all connections to this node
     *
     * This will kill all connections that have been created to this node.
     */
    void close_active_connections();

    /**
     * @brief Check and fix replication
     */
    bool fix_replication();

    /**
     * @brief revert_nodes_snapshot Execute MDBCI snapshot revert command for all nodes
     * @return true in case of success
     */
    bool revert_nodes_snapshot();

    /**
     * @brief prepare_server Initialize MariaDB setup (run mysql_install_db) and create test users
     * Tries to detect Mysql 5.7 installation and disable 'validate_password' pluging
     * @param i Node index
     * @return 0 in case of success
     */
    virtual int prepare_server(int i);
    int prepare_servers();

private:

    int check_node_ssh(int node);
    bool check_master_node(MYSQL *conn);
    bool bad_slave_thread_status(MYSQL *conn, const char *field, int node);
};

class Galera_nodes : public Mariadb_nodes
{
public:

    Galera_nodes(const char *pref, const char *test_cwd, bool verbose) :
        Mariadb_nodes(pref, test_cwd, verbose) { }

    int start_galera();

    virtual int start_replication()
    {
        return start_galera();
    }

    int check_galera();

    virtual int check_replication()
    {
        return check_galera();
    }

    //int prepare_galera_server(int i);

    //virtual int prepare_server(int i)
    //{
    //    return prepare_galera_server(i);
    //}
};

#endif // MARIADB_NODES_H
