/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#ifdef SS_DEBUG
#include "trx_test.hh"
#define BOOST_SPIRIT_X3_HIDE_CXX17_WARNING
#include <boost/fusion/adapted/std_pair.hpp>
#include <boost/spirit/home/x3.hpp>

#include <memory>
#include <fstream>

namespace
{

std::map<std::string, std::string> parse_trx_rc(const std::string& str)
{
    namespace x3 = boost::spirit::x3;

    if (str.empty())
    {
        return {};
    }

    std::map<std::string, std::string> result;
    const auto skipper = x3::space | x3::lexeme['#' >> *(x3::char_ - x3::eol) >> x3::eol];
    const auto key_value_parser = x3::lexeme[+x3::char_("_a-zA-Z0-9")] >> '=' >> x3::lexeme[+x3::graph];

    auto first = str.begin();
    auto success = x3::phrase_parse(first, str.end(), *key_value_parser, skipper, result);

    if (success && first == end(str))
    {
        return result;
    }
    else
    {
        MXB_SERROR("Invalid key-value string: '" << str << '\'');
        return {};
    }
}
}

namespace pinloki
{

// Test variables.

// DBG_PINLOKI_FAIL_MID_TRX - integer, fail on the Nth call.
// Exit with incomplete commit files after the Nth write.
// Expected: On restart log a warning that an incomplete trx (gtid)
// was deleted, connect to the master and receive the same trx (gtid) again.

// DBG_PINLOKI_FAIL_AFTER_TRX_COMMIT - integer, fail on the Nth call.
// Exit after writing and flushing the Nth commit files.
// Expected: On restart the transaction should be recovered and a
// warning added to the log.

// DBG_PINLOKI_FAIL_STARTUP_RECOVERY_SOFT - integer.
// Same as DBG_PINLOKI_FAIL_AFTER_TRX_COMMIT with the addition
// that after at least two binlogs the latest binlog is deleted
// before exit.
// Expected: On restart an informative error message (binlog mismatch,
// missing files or manual intervention) in the log and recovery
// files deleted.

// DBG_PINLOKI_FAIL_STARTUP_RECOVERY_HARD - integer, fail on the Nth call.
// Same as DBG_PINLOKI_FAIL_AFTER_TRX_COMMIT with the addition
// that after at least two binlogs one byte is removed from the
// latest binlog before exit.
// Expected: On restart eror in log and maxscale refuses to start.
// The same error, different log, would happen if recovery write fails.

// These variables are mutually exclusive and specified in <binlogdir>/trx-crash.rc.
// When running tests, also set the pinloki config value "transaction_buffer_size = 2K".
// example rc:
// #DBG_PINLOKI_FAIL_MID_TRX = 50
// DBG_PINLOKI_FAIL_AFTER_TRX_COMMIT = 5
// #DBG_PINLOKI_FAIL_STARTUP_RECOVERY_SOFT = 50
// #DBG_PINLOKI_FAIL_STARTUP_RECOVERY_HARD = 50

CrashTest::CrashTest(InventoryWriter* pInv)
    : m_inventory(*pInv)
{
    std::ifstream rc{pInv->config().binlog_dir_path() + '/' + "trx-crash.rc"};
    if (rc)
    {
        std::stringstream ss;
        ss << rc.rdbuf();

        auto key_values = parse_trx_rc(ss.str());
        if (key_values.size() > 1)
        {
            MXB_SERROR("Only one of the crash-test variables can be defined. Check trx-crash.rc");
        }
        else if (!key_values.empty())
        {
            const auto& kv = *key_values.begin();
            if (kv.first == "DBG_PINLOKI_FAIL_MID_TRX")
            {
                m_fail_mid_trx = std::stoi(kv.second);
            }
            else if (kv.first == "DBG_PINLOKI_FAIL_AFTER_TRX_COMMIT")
            {
                m_fail_after_commit = std::stoi(kv.second);
            }
            else if (kv.first == "DBG_PINLOKI_FAIL_STARTUP_RECOVERY_SOFT")
            {
                m_fail_after_commit = std::stoi(kv.second);
                m_startup_recovery_soft = true;
            }
            else
            {
                MXB_SERROR("Unknown variable in trx-crash.rc: " << kv.first);
            }
        }
    }
}

bool CrashTest::fail_mid_trx()
{
    if (m_fail_mid_trx)
    {
        if (--m_fail_mid_trx == 0)
        {
            return true;
        }
    }

    return false;
}

bool CrashTest::fail_after_commit()
{
    if (m_fail_after_commit)
    {
        if (--m_fail_after_commit == 0)
        {
            return true;
        }
    }

    return false;
}

bool CrashTest::startup_recovery_soft()
{
    if (m_startup_recovery_soft && m_fail_after_commit == 1)
    {
        if (m_inventory.file_names().size() > 2)
        {
            ::remove(last_string(m_inventory.file_names()).c_str());
            return true;
        }

        // Don't let m_fail_after_commit go to zero, depends on order of calls to
        // fail_after_commit and startup_recovery_soft, which should be called first.
        ++m_fail_after_commit;
    }

    return false;
}

static std::unique_ptr<CrashTest> sStatic_crash_test;

void init_crash_test(InventoryWriter* pInv)
{
    sStatic_crash_test = std::make_unique<CrashTest>(pInv);
}
CrashTest& crash_test()
{
    mxb_assert(sStatic_crash_test);
    return *sStatic_crash_test;
}
}
#endif
