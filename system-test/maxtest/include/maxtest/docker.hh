/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxtest/testconnections.hh>
#include <string_view>
#include <iostream>

namespace maxtest
{
/**
 * Simple RAII class for starting a docker image
 */
class Docker
{
public:
    Docker(const Docker&) = delete;
    Docker& operator=(const Docker&) = delete;

    /**
     * Start a docker container on the first MaxScale VM
     *
     * @param test      TestConnections instance
     * @param image     The image to start
     * @param name      The container name
     * @param ports     List of ports that are forwarded to the host
     * @param env       List of KEY:VALUE environment variables set for the image
     * @param args      The arguments given to the container (optional)
     * @param check_cmd Command that's used to check when the container is ready for use (optional)
     *
     * @throws std::runtime_error if the container startup fails or if the check command
     *         fails for over 30 seconds.
     */
    Docker(TestConnections& test, std::string_view image, std::string_view name,
           std::initializer_list<int> ports, std::initializer_list<std::string_view> env,
           std::string_view args = "", std::string_view check_cmd = "")
        : m_test(test)
        , m_name(name)
    {
        // Remove any stale containers that have the same name
        cleanup();

        std::ostringstream cmd;
        // The --privileged flag works around this problem that's encoutered with
        // older Docker releases: https://github.com/moby/moby/issues/42680
        cmd << "docker run --privileged -d --rm --name " << name << " ";

        for (auto p : ports)
        {
            cmd << "-p " << p << ":" << p << " ";
        }

        for (auto e : env)
        {
            cmd << "-e " << e << " ";
        }

        cmd << image << " " << args;

        auto res = m_test.maxscale->ssh_output(cmd.str());

        if (res.rc == 0)
        {
            if (!check_cmd.empty())
            {
                cmd.str("");
                cmd << "docker exec " << name << " " << check_cmd;
                std::string check = cmd.str();

                for (int i = 0; i < 30; i++)
                {
                    res = m_test.maxscale->ssh_output(check);

                    if (res.rc == 0)
                    {
                        break;
                    }
                    else
                    {
                        std::this_thread::sleep_for(1s);
                    }
                }

                if (res.rc != 0)
                {
                    throw problem("Container check command '", check, "' failed: ", res.rc, ", ", res.output);
                }
            }
        }
        else
        {
            throw problem("Failed to start image '", image, "': ", res.rc, ", ", res.output);
        }
    }

    /**
     * Executes a command inside the container
     *
     * @param cmd Command to execute
     *
     * @return The exit code of the command
     */
    int execute(std::string_view cmd)
    {
        return m_test.maxscale->ssh_node(mxb::cat("docker exec -u root ", m_name, " ", cmd), true);
    }

    ~Docker()
    {
        m_test.expect(cleanup(), "Failed to stop container");
    }

private:

    template<class ... Args>
    std::runtime_error problem(Args&& ... args)
    {
        std::ostringstream ss;
        (ss << ... << args);
        return std::runtime_error(ss.str());
    }

    bool cleanup()
    {
        auto res = m_test.maxscale->ssh_output(mxb::cat("docker rm -vf ", m_name));
        return res.rc == 0;
    }

    TestConnections& m_test;
    std::string      m_name;
};
}
