/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
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

class RebuildServer : public Operation
{
public:
    RebuildServer(MariaDBMonitor& mon, SERVER* target, SERVER* source, MariaDBServer* master);

    bool   run() override;
    Result result() override;
    void   cancel() override;

private:
    MariaDBMonitor& m_mon;

    SERVER*        m_target_srv {nullptr};
    SERVER*        m_source_srv {nullptr};
    MariaDBServer* m_target {nullptr};
    MariaDBServer* m_source {nullptr};
    MariaDBServer* m_repl_master {nullptr};

    SlaveStatusArray m_source_slaves_old;
    bool             m_source_slaves_stopped {false};

    int                  m_rebuild_port {0};
    std::chrono::seconds m_ssh_timeout {0};
    std::chrono::seconds m_port_open_delay {0};

    ssh_util::SSession m_target_ses;
    ssh_util::SSession m_source_ses;

    std::unique_ptr<ssh_util::AsyncCmd> m_target_cmd;
    std::unique_ptr<ssh_util::AsyncCmd> m_source_cmd;

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

    Result m_result;

    bool init();
    void test_datalink();
    bool serve_backup();
    bool prepare_target();
    bool start_transfer();
    bool wait_transfer();
    void check_datadir_size();
    bool start_backup_prepare();
    bool wait_backup_prepare();

    bool start_target();
    bool start_replication();
    void cleanup();

    bool rebuild_check_preconds();
    bool check_rebuild_tools(MariaDBServer* server, ssh::Session& ssh);
    bool check_free_listen_port(ssh::Session& ses, MariaDBServer* server);
    bool run_cmd_on_target(const std::string& cmd, const std::string& desc);
    void report_source_stream_status();

    MariaDBServer* autoselect_source_srv(const MariaDBServer* target);
};
}
