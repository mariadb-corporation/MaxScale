/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once
#include "mariadbmon_common.hh"
#include <condition_variable>
#include <atomic>
#include <memory>
#include <mutex>
#include "ssh_utils.hh"
#include "server_utils.hh"

class SERVER;
class MariaDBServer;
class MariaDBMonitor;

namespace mon_op
{

struct Result
{
    Result deep_copy() const;

    bool success {false};

    // Error printing functions interpret an "undefined" json-object as null and won't print to it.
    mxb::Json output {mxb::Json::Type::OBJECT};
};

/**
 * This class represents two related things: manual commands (such as manual failover) and long-running
 * automatic commands (such as automatic rebuild-server). These two types are similar in the sense that
 * both block the scheduling of further manual commands and are ran at the end of a monitor tick.
 */
class Operation
{
public:
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;

    Operation() = default;
    virtual ~Operation() = default;

    virtual bool   run() = 0;
    virtual Result result() = 0;
    virtual void   cancel() = 0;
};

using SOperation = std::unique_ptr<Operation>;

enum class ExecState
{
    SCHEDULED,
    RUNNING,
    DONE
};

struct ResultInfo
{
    Result      res;
    std::string cmd_name;
};

/**
 * Stores info on the currently scheduled/running operation. Deals mostly with manual commands. Long-
 * running automatic ops may modify some fields to prevent the user to schedule another op.
 */
struct OperationInfo
{
    std::mutex lock;        /**< Most fields must be protected from concurrent access. */
    SOperation scheduled_op;/**< Manually scheduled operation. */

    /** State of manual op or long-running auto op. Both cannot be scheduled/running simultaneously. */
    std::atomic<ExecState> exec_state {ExecState::DONE};
    /**< True if monitor stop was requested. Prevents starting another operation. */
    std::atomic_bool monitor_stopping {true};
    std::atomic_bool cancel_op {false};     /**< True if cancel has been requested for a running op */

    std::string op_name;    /**< Either name of the scheduled op or name of current long-running auto op. */

    bool current_op_is_manual {false};      /** True if the currently scheduled/running op is manual. */

    std::condition_variable     result_ready_notifier;
    std::unique_ptr<ResultInfo> result_info;
};

using CmdMethod = std::function<Result (void)>;

/**
 * An operation, likely manual, which completes in one monitor iteration. Does not have internal state.
 */
class SimpleOp : public Operation
{
public:
    explicit SimpleOp(CmdMethod func);

    bool   run() override;
    Result result() override;
    void   cancel() override;

private:
    CmdMethod m_func;
    Result    m_result;
};

class BackupOperation : public Operation
{
public:
    BackupOperation(MariaDBMonitor& mon);
    Result result() override;

protected:
    enum class StateResult {OK, AGAIN, ERROR};
    enum class TargetOwner {ROOT, NORMAL};

    bool        init_operation();
    bool        test_datalink();
    bool        serve_backup(const std::string& mariadb_user, const std::string& mariadb_pw);
    bool        start_transfer(const std::string& destination, TargetOwner target_owner);
    StateResult wait_transfer();
    bool        start_backup_prepare();
    StateResult wait_backup_prepare();
    bool        start_target();
    bool        start_replication(MariaDBServer* target, MariaDBServer* repl_master);
    bool        check_datadir(std::shared_ptr<ssh::Session> ses, const std::string& datadir_path);
    void        cleanup(MariaDBServer* source, const SlaveStatusArray& source_slaves_old);

    ssh_util::SSession init_ssh_session(const char* name, const std::string& host);
    bool               check_rebuild_tools(const char* srvname, ssh::Session& ssh);
    bool               check_free_listen_port(const char* srvname, ssh::Session& ssh, int port);
    bool               check_ssh_settings();
    bool               check_backup_storage_settings();
    bool               check_server_is_valid_target(const MariaDBServer* target);
    bool               check_server_is_valid_source(const MariaDBServer* source);
    void               report_source_stream_status();
    MariaDBServer*     autoselect_source_srv(const MariaDBServer* target);
    bool               run_cmd_on_target(const std::string& cmd, const std::string& desc);
    bool               prepare_target();

    using DirCheckFunc = std::function<bool (const ssh_util::FileInfo&)>;
    bool check_directory_entries(std::shared_ptr<ssh::Session> ses, const std::string& srv_name,
                                 const std::string& path, const DirCheckFunc& check_func);

    void set_source(std::string name, std::string host);
    void set_target(std::string name, std::string host);

    int source_port() const;

    MariaDBMonitor&      m_mon;
    std::chrono::seconds m_ssh_timeout {0};
    std::chrono::seconds m_port_open_delay {0};
    bool                 m_source_slaves_stopped {false};
    Result               m_result;

    std::string                         m_source_name;
    std::string                         m_source_host;
    ssh_util::SSession                  m_source_ses;
    std::unique_ptr<ssh_util::AsyncCmd> m_source_cmd;

    std::string                         m_target_name;
    std::string                         m_target_host;
    ssh_util::SSession                  m_target_ses;
    std::unique_ptr<ssh_util::AsyncCmd> m_target_cmd;

private:
    int m_source_port {0};
};

class RebuildServer : public BackupOperation
{
public:
    RebuildServer(MariaDBMonitor& mon, SERVER* target, SERVER* source);

    bool run() override;
    void cancel() override;

private:
    SERVER*          m_target_srv {nullptr};
    SERVER*          m_source_srv {nullptr};
    MariaDBServer*   m_target {nullptr};
    MariaDBServer*   m_source {nullptr};
    SlaveStatusArray m_source_slaves_old;

    enum class State
    {
        INIT,
        TEST_DATALINK,
        SERVE_BACKUP,
        PREPARE_TARGET,
        START_TRANSFER,
        WAIT_TRANSFER,
        CHECK_DATADIR_SIZE,
        START_BACKUP_PREPARE,
        WAIT_BACKUP_PREPARE,
        START_TARGET,
        START_REPLICATION,
        DONE,
        CLEANUP,
    };
    State m_state {State::INIT};

    bool state_init();
    bool state_test_datalink();
    bool state_serve_backup();
    bool state_prepare_target();
    bool state_start_transfer();
    bool state_wait_transfer();
    void state_check_datadir();
    bool state_start_backup_prepare();
    bool state_wait_backup_prepare();
    bool state_start_target();
    bool state_start_replication();
    void state_cleanup();

    bool check_preconditions();
};

class CreateBackup : public BackupOperation
{
public:
    CreateBackup(MariaDBMonitor& mon, SERVER* source, std::string bu_name);

    bool run() override;
    void cancel() override;

private:
    SERVER*          m_source_srv {nullptr};
    MariaDBServer*   m_source {nullptr};
    SlaveStatusArray m_source_slaves_old;
    std::string      m_bu_name;
    std::string      m_bu_path;

    enum class State
    {
        INIT,
        TEST_DATALINK,
        CHECK_BACKUP_STORAGE,
        SERVE_BACKUP,
        START_TRANSFER,
        WAIT_TRANSFER,
        CHECK_BACKUP_SIZE,
        DONE,
        CLEANUP,
    };
    State m_state {State::INIT};

    bool state_init();
    bool state_test_datalink();
    bool state_check_backup_storage();
    bool state_serve_backup();
    bool state_start_transfer();
    bool state_wait_transfer();
    void state_check_backup();
    void state_cleanup();

    bool check_preconditions();
};

class RestoreFromBackup : public BackupOperation
{
public:
    RestoreFromBackup(MariaDBMonitor& mon, SERVER* target, std::string bu_name);

    bool run() override;
    void cancel() override;

private:
    SERVER*        m_target_srv {nullptr};
    MariaDBServer* m_target {nullptr};
    std::string    m_bu_name;
    std::string    m_bu_path;

    enum class State
    {
        INIT,
        TEST_DATALINK,
        CHECK_BACKUP_STORAGE,
        SERVE_BACKUP,
        PREPARE_TARGET,
        START_TRANSFER,
        WAIT_TRANSFER,
        CHECK_DATADIR_SIZE,
        START_BACKUP_PREPARE,
        WAIT_BACKUP_PREPARE,
        START_TARGET,
        START_REPLICATION,
        DONE,
        CLEANUP,
    };
    State m_state {State::INIT};

    bool state_init();
    bool state_test_datalink();
    bool state_check_backup_storage();
    bool state_serve_backup();
    bool state_prepare_target();
    bool state_start_transfer();
    bool state_wait_transfer();
    void state_check_datadir();
    bool state_start_backup_prepare();
    bool state_wait_backup_prepare();
    bool state_start_target();
    bool state_start_replication();
    void state_cleanup();

    bool check_preconditions();
};
}
