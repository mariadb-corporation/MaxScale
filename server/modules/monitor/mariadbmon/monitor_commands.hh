/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
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

namespace mon_op
{

struct Result
{
    ~Result();
    void deep_copy_from(const Result& rhs);

    bool    success {false};
    json_t* output {nullptr};
};

/**
 * This class represents two related things: manual commands (such as manual failover) and long-running
 * automatic commands (such as automatic rebuild-server). These two types are similar in the sense that
 * both block the scheduling of further manual commands and are ran at the end of a monitor tick.
 */
class Operation
{
public:
    virtual ~Operation() = default;
    virtual bool   run() = 0;
    virtual Result result() = 0;
    virtual bool   cancel() = 0;
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
    bool   cancel() override;

private:
    CmdMethod m_func;
    Result    m_result;
};
}
