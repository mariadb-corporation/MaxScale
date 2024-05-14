/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/log.hh>

#include <cstdarg>
#include <future>
#include <sys/time.h>
#include <maxbase/format.hh>
#include <maxbase/string.hh>
#include <maxbase/threadpool.hh>
#include <maxbase/semaphore.hh>

using std::string;
namespace
{
const int sec_to_us = std::micro::den;
// Command which gets info on all containers.
const string docker_info_cmd = "curl --silent --unix-socket /var/run/docker.sock v1.4/containers/json";
mxb::ThreadPool threadpool;
}

namespace maxtest
{
void TestLogger::add_failure(const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    add_failure_v(format, valist);
    va_end(valist);
}

void TestLogger::add_failure_v(const char* format, va_list args)
{
    string msg = prepare_msg(format, args);
    string timeinfo = time_string();

    printf("%s: TEST_FAILED! %s\n", timeinfo.c_str(), msg.c_str());
    fflush(stdout);
    string full_msg;
    full_msg.reserve(timeinfo.length() + 2 + msg.length());
    full_msg.append(timeinfo).append(": ").append(msg);
    std::lock_guard<std::mutex> guard(m_lock);
    m_fails.push_back(move(full_msg));
    m_n_fails++;
}

void TestLogger::expect(bool result, const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    expect_v(result, format, valist);
    va_end(valist);
}

void TestLogger::expect_v(bool result, const char* format, va_list args)
{
    if (!result)
    {
        add_failure_v(format, args);
    }
}

TestLogger::TestLogger()
{
    reset_timer();
}

std::string TestLogger::all_errors_to_string()
{
    string rval;
    std::lock_guard<std::mutex> guard(m_lock);
    rval = mxb::create_list_string(m_fails, "\n");
    return rval;
}

std::string TestLogger::latest_error()
{
    std::lock_guard<std::mutex> guard(m_lock);
    return m_fails.back();
}

void TestLogger::log_msgf(const char* format, ...)
{
    va_list valist;
    va_start(valist, format);
    log_msg(format, valist);
    va_end(valist);
}

void TestLogger::log_msg(const char* format, va_list args)
{
    string msg = prepare_msg(format, args);
    string timeinfo = time_string();
    printf("%s: %s\n", timeinfo.c_str(), msg.c_str());
}

void TestLogger::log_msg(const string& str)
{
    log_msgf("%s", str.c_str());
}

std::string TestLogger::time_string() const
{
    timeval now {0};
    gettimeofday(&now, nullptr);

    // Generate time stamp.
    struct tm broken_down_time { 0 };
    localtime_r(&now.tv_sec, &broken_down_time);
    char timebuf[10];
    strftime(timebuf, sizeof(timebuf), "%T", &broken_down_time);

    int64_t elapsed_time_us = (sec_to_us * now.tv_sec + now.tv_usec) - m_start_time_us;
    double elapsed_time_s = (double)elapsed_time_us / sec_to_us;

    return mxb::string_printf("%s %5.1fs", timebuf, elapsed_time_s);
}

std::string TestLogger::prepare_msg(const char* format, va_list args) const
{
    string msg = mxb::string_vprintf(format, args);
    if (!msg.empty() && msg.back() == '\n')
    {
        msg.pop_back();
    }
    return msg;
}

void TestLogger::reset_timer()
{
    timeval now {0};
    gettimeofday(&now, nullptr);
    m_start_time_us = sec_to_us * now.tv_sec + now.tv_usec;
}

int TestLogger::time_elapsed_s() const
{
    auto now_s = time(nullptr);
    auto start_s = m_start_time_us / sec_to_us;
    return now_s - start_s;
}

bool SharedData::concurrent_run(const BoolFuncArray& funcs)
{
    bool rval = true;
    if (settings.allow_concurrent_run)
    {
        auto n = funcs.size();
        bool results[n];
        mxb::Semaphore sem;

        for (size_t i = 0; i < n; i++)
        {
            auto pool_task = [&funcs, &results, &sem, i]() {
                    results[i] = funcs[i]();
                    sem.post();
                };
            threadpool.execute(pool_task, "log");
        }

        sem.wait_n(n);

        for (auto res : results)
        {
            if (!res)
            {
                rval = false;
            }
        }
    }
    else
    {
        for (auto& func : funcs)
        {
            if (!func())
            {
                rval = false;
            }
        }
    }
    return rval;
}

bool SharedData::run_shell_command(const string& cmd, const string& errmsg)
{
    bool rval = true;
    auto cmdc = cmd.c_str();

    int rc = system(cmdc);
    if (rc != 0)
    {
        rval = false;
        string msgp2 = mxb::string_printf("Shell command '%s' returned %i.", cmdc, rc);
        if (errmsg.empty())
        {
            log.add_failure("%s", msgp2.c_str());
        }
        else
        {
            log.add_failure("%s %s", errmsg.c_str(), msgp2.c_str());
        }
    }
    return rval;
}

bool SharedData::run_shell_cmdf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::string cmd = mxb::string_vprintf(fmt, args);
    va_end(args);
    return run_shell_command(cmd, "");
}

mxt::CmdResult SharedData::run_shell_cmd_output(const string& cmd)
{
    mxt::CmdResult rval;
    FILE* pipe = popen(cmd.c_str(), "r");
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
        log.add_failure("popen() failed when running command '%s' on the local machine.", cmd.c_str());
    }
    return rval;
}

mxt::CmdResult SharedData::run_shell_cmd_outputf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::string cmd = mxb::string_vprintf(fmt, args);
    va_end(args);
    return run_shell_cmd_output(cmd);
}

/**
* Read key value from MDBCI network config contents.
*
* @param nwconfig File contents as a map
* @param search_key Name of field to read
* @return value of variable or empty value if not found
*/
std::string SharedData::get_nc_item(const NetworkConfig& nwconfig, const string& search_key)
{
    string rval;
    auto it = nwconfig.find(search_key);
    if (it != nwconfig.end())
    {
        rval = it->second;
    }

    if (settings.verbose)
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

bool SharedData::read_str(const mxb::ini::map_result::ConfigSection& cnf, const string& key, string& dest)
{
    bool rval = false;
    auto& kvs = cnf.key_values;
    auto it = kvs.find(key);
    if (it == kvs.end())
    {
        log.add_failure("Parameter '%s' is missing.", key.c_str());
    }
    else
    {
        dest = it->second.value;
        rval = true;
    }
    return rval;
}

bool SharedData::read_int(const mxb::ini::map_result::ConfigSection& cnf, const string& key, int& dest)
{
    bool rval = false;
    auto& kvs = cnf.key_values;
    auto it = kvs.find(key);
    if (it == kvs.end())
    {
        log.add_failure("Parameter '%s' is missing.", key.c_str());
    }
    else
    {
        const char* val = it->second.value.c_str();
        if (mxb::get_int(val, &dest))
        {
            rval = true;
        }
        else
        {
            log.add_failure("'%s' is not a valid integer.", val);
        }
    }
    return rval;
}

bool SharedData::update_docker_container_info()
{
    bool rval = false;
    auto containers_info = run_shell_cmd_output(docker_info_cmd);
    if (containers_info.rc == 0)
    {
        mxb::Json data;
        if (data.load_string(containers_info.output))
        {
            rval = true;
            DockerInfoMap new_info;

            auto arr = data.get_array_elems();
            for (auto& elem : arr)
            {
                bool elem_ok = false;
                auto names_arr = elem.get_array_elems("Names");
                if (names_arr.size() == 1)
                {
                    string name = names_arr[0].get_string();
                    if (!name.empty() && name[0] == '/')
                    {
                        name.erase(0, 1);
                        new_info.insert({std::move(name), std::move(elem)});
                        elem_ok = true;
                    }
                }

                if (!elem_ok)
                {
                    log.add_failure("Invalid data from '%s'.", docker_info_cmd.c_str());
                    rval = false;
                }
            }

            if (rval)
            {
                std::lock_guard<std::mutex> lock(m_docker_container_info_lock);
                m_docker_container_info = std::move(new_info);
            }
        }
    }
    return rval;
}

mxb::Json SharedData::get_container_info(const string& container) const
{
    mxb::Json rval;
    std::lock_guard<std::mutex> lock(m_docker_container_info_lock);
    auto it = m_docker_container_info.find(container);
    if (it != m_docker_container_info.end())
    {
        rval = it->second;
    }
    return rval;
}

std::string cutoff_string(const string& source, char cutoff)
{
    auto pos = source.find(cutoff);
    return (pos != string::npos) ? source.substr(0, pos) : source;
}
}
