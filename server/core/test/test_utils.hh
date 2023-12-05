#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>
#include <maxbase/maxbase.hh>
#include <maxbase/stacktrace.hh>
#include <maxbase/watchdognotifier.hh>
#include <maxscale/built_in_modules.hh>
#include <maxscale/cachingparser.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/dcb.hh>
#include <maxscale/listener.hh>
#include <maxscale/log.hh>
#include <maxscale/maxscale_test.h>
#include <maxscale/paths.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/test.hh>
#include <maxscale/threadpool.hh>

#include <sys/stat.h>
#include <openssl/ssl.h>

#include "../internal/filter.hh"
#include "../internal/maxscale.hh"
#include "../internal/modules.hh"
#include "../internal/monitormanager.hh"
#include "../internal/servermanager.hh"
#include "../internal/service.hh"

/**
 * Preload a module
 *
 * If the test uses code that is not a part of the core, the module must be preloaded before the test is
 * started. In most cases this is only required for module-level unit tests.
 */
void preload_module(const char* name, const char* path, mxs::ModuleType type)
{
    std::string old_libdir = mxs::libdir();
    std::string fullpath = TEST_DIR;
    fullpath += "/";
    fullpath += path;
    mxs::set_libdir(fullpath.c_str());
    get_module(name, type);
    mxs::set_libdir(old_libdir.c_str());
}

static int set_signal(int sig, void (* handler)(int));

static void sigfatal_handler(int i)
{
    set_signal(i, SIG_DFL);
    mxb::dump_stacktrace(
        [](const char* cmd) {
        MXB_ALERT("  %s", cmd);
    });
    raise(i);
}

static int set_signal(int sig, void (* handler)(int))
{
    int rc = 0;

    struct sigaction sigact = {};
    sigact.sa_handler = handler;

    int err;

    do
    {
        errno = 0;
        err = sigaction(sig, &sigact, NULL);
    }
    while (errno == EINTR);

    if (err < 0)
    {
        MXB_ERROR("Failed call sigaction() in %s due to %d, %s.",
                  program_invocation_short_name,
                  errno,
                  mxb_strerror(errno));
        rc = 1;
    }

    return rc;
}

static maxbase::WatchdogNotifier* watchdog_notifier = nullptr;

/**
 * Initialize test environment
 *
 * This initializes all libraries required to run unit tests. If worker related functionality is required, use
 *`run_unit_test` instead.
 */
void init_test_env(char* __attribute((unused)) path = nullptr)
{
    set_signal(SIGSEGV, sigfatal_handler);
    set_signal(SIGABRT, sigfatal_handler);
    set_signal(SIGILL, sigfatal_handler);
    set_signal(SIGFPE, sigfatal_handler);
#ifdef SIGBUS
    set_signal(SIGBUS, sigfatal_handler);
#endif

    const char* argv = "maxscale";
    mxs::Config::init(1, (char**)&argv);

    mxs::Config::get().n_threads = 1;

    SSL_library_init();
    SSL_load_error_strings();
    OPENSSL_add_all_algorithms_noconf();

    if (!mxs_log_init(NULL, NULL, MXB_LOG_TARGET_STDOUT))
    {
        exit(1);
    }
    atexit(mxs_log_finish);
    std::string old_libdir = mxs::libdir();
    mxs::set_libdir(TEST_DIR "/server/modules/parser_plugin/pp_sqlite/");
    maxbase::init();
    watchdog_notifier = new mxb::WatchdogNotifier(0);
    maxscale::RoutingWorker::init(watchdog_notifier);

    add_built_in_module(mariadbprotocol_info());
    add_built_in_module(mariadbauthenticator_info());
    mxs::set_libdir(old_libdir.c_str());
    preload_module("readconnroute", "server/modules/routing/readconnroute/", mxs::ModuleType::ROUTER);
}

/**
 * Runs the function on a worker thread after preparing the test environment
 *
 * This function should be used if any of the core objects (sessions, services etc.) are needed. If only
 * library functions are tested, `init_test_env` is sufficient.
 */
void run_unit_test(std::function<void ()> func)
{
    mxs::test::start_test();
    init_test_env();

    mxs::MainWorker main_worker(watchdog_notifier);

    main_worker.execute([&func]() {
        mxs::RoutingWorker::start_workers(config_threadcount());

        func();

        maxscale_shutdown();
    }, mxb::Worker::EXECUTE_QUEUED);

    main_worker.run();

    mxs::thread_pool().stop(false);
    mxs::RoutingWorker::join_workers();
    MonitorManager::destroy_all_monitors();
    maxscale_start_teardown();
    service_destroy_instances();
    filter_destroy_instances();
    mxs::Listener::clear();
    ServerManager::destroy_all();
}
