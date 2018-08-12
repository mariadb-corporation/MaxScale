/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "../internal/event.hh"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <maxscale/debug.h>
#include <maxscale/log_manager.h>

using namespace maxscale;
using namespace std;

namespace
{

struct NAME_AND_VALUE
{
    const char* zName;
    int32_t     value;
};

const NAME_AND_VALUE levels[] =
{
    {
        "LOG_ALERT",
        LOG_ALERT,
    },
    {
        "LOG_CRIT",
        LOG_CRIT,
    },
    {
        "LOG_DEBUG",
        LOG_DEBUG,
    },
    {
        "LOG_EMER",
        LOG_EMERG,
    },
    {
        "LOG_ERR",
        LOG_ERR,
    },
    {
        "LOG_INFO",
        LOG_INFO,
    },
    {
        "LOG_NOTICE",
        LOG_NOTICE,
    },
    {
        "LOG_WARNING",
        LOG_WARNING,
    },
    {
        "BLAH",
        -1
    }
};

const int N_LEVELS = sizeof(levels) / sizeof(levels[0]);

// Keep these in alphabetical order.
const NAME_AND_VALUE facilities[] =
{
    {
        "LOG_AUTH",
        LOG_AUTH
    },
    {
        "LOG_AUTHPRIV",
        LOG_AUTHPRIV
    },
    {
        "LOG_CRON",
        LOG_CRON
    },
    {
        "LOG_DAEMON",
        LOG_DAEMON
    },
    {
        "LOG_FTP",
        LOG_FTP
    },
    {
        "LOG_KERN",
        LOG_KERN
    },
    {
        "LOG_LOCAL0",
        LOG_LOCAL0
    },
    {
        "LOG_LOCAL1",
        LOG_LOCAL1
    },
    {
        "LOG_LOCAL2",
        LOG_LOCAL2
    },
    {
        "LOG_LOCAL3",
        LOG_LOCAL3
    },
    {
        "LOG_LOCAL4",
        LOG_LOCAL4
    },
    {
        "LOG_LOCAL5",
        LOG_LOCAL5
    },
    {
        "LOG_LOCAL6",
        LOG_LOCAL6
    },
    {
        "LOG_LOCAL7",
        LOG_LOCAL0
    },
    {
        "LOG_LPR",
        LOG_LPR
    },
    {
        "LOG_MAIL",
        LOG_MAIL
    },
    {
        "LOG_NEWS",
        LOG_NEWS
    },
    {
        "LOG_SYSLOG",
        LOG_SYSLOG
    },
    {
        "LOG_USER",
        LOG_USER
    },
    {
        "LOG_UUCP",
        LOG_UUCP
    },
    {
        "BLAH",
        -1
    }
};

const int N_FACILITIES = sizeof(facilities) / sizeof(facilities[0]);


template<class T>
int test_names_and_values(const NAME_AND_VALUE* begin,
                          const NAME_AND_VALUE* end,
                          bool (*from_string)(T*, const char* zName),
                          const char* (*to_string)(T),
                          const char* zProperty)
{
    int errors = 0;

    for_each(begin, end, [&errors, zProperty, from_string, to_string](const NAME_AND_VALUE& item)
             {
                 T value;
                 bool rv = from_string(&value, item.zName);

                 if (item.value != -1)
                 {
                     if (rv)
                     {
                         if (value != item.value)
                         {
                             ++errors;
                             cerr << "error: Wrong " << zProperty << " was returned for "
                                  << item.zName << ", " << item.value << " was expected, but "
                                  << value << " was returned." << endl;
                         }
                     }
                     else
                     {
                         ++errors;
                         cerr << "error: " << item.zName << " was not recognized as a syslog "
                              << zProperty << "." << endl;
                     }

                     const char* zName = to_string(static_cast<T>(item.value));

                     if (strcmp(zName, item.zName) != 0)
                     {
                         ++errors;
                         cerr << "error: Code " << item.value << " was converted to " << zName
                              << " although " << item.zName << " was expected." << endl;
                     }
                 }
                 else
                 {
                     if (rv)
                     {
                         ++errors;
                         cerr << "error: " << item.zName << " was incorrectly recognized "
                              << "as a syslog " << zProperty << " salthough it should not "
                              << "have been." << endl;
                     }

                     const char* zName = to_string(static_cast<T>(item.value));

                     if (strcmp(zName, "Unknown") != 0)
                     {
                         cerr << "error: Invalid code " << item.value << " was not converted "
                              << "to Unknown as expected but to " << zName << "." << endl;
                     }
                 }
             });

    return errors;
}

int test_levels()
{
    const NAME_AND_VALUE* begin = levels;
    const NAME_AND_VALUE* end = begin + N_LEVELS;

    return test_names_and_values(begin, end, &log_level_from_string, &log_level_to_string, "level");
}

int test_facilities()
{
    const NAME_AND_VALUE* begin = facilities;
    const NAME_AND_VALUE* end = facilities + N_LEVELS;

    return test_names_and_values(begin, end, &log_facility_from_string, &log_facility_to_string, "facility");
}


const NAME_AND_VALUE events[] =
{
    {
        "authentication_failure",
        event::AUTHENTICATION_FAILURE
    }
};

const int N_EVENTS = sizeof(events) / sizeof(events[0]);

int test_event_basics()
{
    int errors = 0;

    const NAME_AND_VALUE* begin = events;
    const NAME_AND_VALUE* end = events + N_EVENTS;

    errors += test_names_and_values(begin, end, &event::from_string, &event::to_string, "event");

    for_each(begin, end, [&errors](const NAME_AND_VALUE& item)
             {
                 event::id_t id = static_cast<event::id_t>(item.value);

                 int32_t facility = event::get_log_facility(id);

                 if (facility != event::DEFAULT_FACILITY)
                 {
                     ++errors;
                     cerr << "error: Default facility for " << event::to_string(id) << " was "
                          << log_facility_to_string(facility) << " and not "
                          << log_facility_to_string(event::DEFAULT_FACILITY)
                          << "." << endl;
                 }

                 event::set_log_facility(id, LOG_LOCAL0);

                 facility = event::get_log_facility(id);

                 if (facility != LOG_LOCAL0)
                 {
                     ++errors;
                     cerr << "error: Set facility LOG_LOCAL0 as not stored, but was "
                          << log_facility_to_string(facility) << "." << endl;
                 }

                 int32_t level = event::get_log_level(id);

                 if (level != event::DEFAULT_LEVEL)
                 {
                     ++errors;
                     cerr << "error: Default level for " << event::to_string(id) << " was "
                          << log_level_to_string(level) << " and not "
                          << log_level_to_string(event::DEFAULT_LEVEL)
                          << "." << endl;
                 }

                 event::set_log_level(id, LOG_ALERT);

                 level = event::get_log_level(id);

                 if (level != LOG_ALERT)
                 {
                     ++errors;
                     cerr << "error: Set level LOG_ALERT as not stored, but was "
                          << log_level_to_string(level) << "." << endl;
                 }
             });

    return errors;
}


enum class What
{
    FACILITY,
    LEVEL,
    IRRELEVANT,
};

struct CONFIGURATION
{
    const char*     zParameter;
    const char*     zValue;
    event::id_t     id;
    event::result_t result;
    What            what;
    int32_t         value;
};

const CONFIGURATION configurations[] =
{
    {
        "event.authentication_failure.facility",
        "LOG_LOCAL0",
        event::AUTHENTICATION_FAILURE,
        event::ACCEPTED,
        What::FACILITY,
        LOG_LOCAL0
    },
    {
        "event.authentication_failure.level",
        "LOG_ALERT",
        event::AUTHENTICATION_FAILURE,
        event::ACCEPTED,
        What::LEVEL,
        LOG_ALERT
    },
    {
        "event.authentication_failure.facility",
        "LOG_BLAH",
        event::AUTHENTICATION_FAILURE,
        event::INVALID,
        What::FACILITY,
        -1
    },
    {
        "event.authentication_failure.level",
        "LOG_BLAH",
        event::AUTHENTICATION_FAILURE,
        event::INVALID,
        What::LEVEL,
        -1
    },
    {
        "event.blah.facility",
        "LOG_LOCAL0",
        static_cast<event::id_t>(-1),
        event::INVALID,
        What::FACILITY,
        LOG_LOCAL0
    },
    {
        "blah",
        "LOG_LOCAL0",
        static_cast<event::id_t>(-1),
        event::IGNORED,
        What::IRRELEVANT,
        LOG_LOCAL0
    }
};

const size_t N_CONFIGURATIONS = sizeof(configurations) / sizeof(configurations[0]);

int test_event_configuration()
{
    int errors = 0;

    const CONFIGURATION* begin = configurations;
    const CONFIGURATION* end = begin + N_CONFIGURATIONS;

    for_each(begin, end, [&errors](const CONFIGURATION& c)
             {
                 event::result_t result = event::configure(c.zParameter, c.zValue);

                 if (result == c.result)
                 {
                     if (result == event::ACCEPTED)
                     {
                         if (c.what == What::FACILITY)
                         {
                             int32_t facility = event::get_log_facility(c.id);

                             if (facility != c.value)
                             {
                                 ++errors;
                                 cerr << "error: Configuration \""
                                      << c.zParameter << "=" << c.zValue
                                      << " did not affect the facility in the expected way."
                                      << endl;
                             }
                         }
                         else
                         {
                             ss_dassert(c.what == What::LEVEL);

                             int32_t level = event::get_log_level(c.id);

                             if (level != c.value)
                             {
                                 ++errors;
                                 cerr << "error: Configuration \""
                                      << c.zParameter << "=" << c.zValue
                                      << " did not affect the level in the expected way."
                                      << endl;
                             }
                         }
                     }
                 }
                 else
                 {
                     ++errors;
                     cerr << "error: Configuration \""
                          << c.zParameter << "=" << c.zValue
                          << " did not produce the expected result."
                          << endl;
                 }
             });

    return errors;
}

int test_events()
{
    int errors = 0;

    errors += test_event_basics();
    errors += test_event_configuration();

    return errors;
}

string get_auth_log()
{
    string name;

    const char DEBIAN_AUTH_LOG[] = "/var/log/auth.log";
    const char REDHAT_AUTH_LOG[] = "/var/log/secure";

    if (access(DEBIAN_AUTH_LOG, F_OK) == 0)
    {
        cout << "notice: " << DEBIAN_AUTH_LOG << " exists, assuming a Debian system." << endl;
        name = DEBIAN_AUTH_LOG;
    }
    else if (access(REDHAT_AUTH_LOG, F_OK) == 0)
    {
        cout << "notice: " << REDHAT_AUTH_LOG << " exists, assuming a RedHat system." << endl;
        name = REDHAT_AUTH_LOG;
    }
    else
    {
        cout << "warning: Neither " << DEBIAN_AUTH_LOG << ", nor " << REDHAT_AUTH_LOG << " exists." << endl;
    }

    return name;
}

int test_logging()
{
    int errors = 0;

    event::set_log_facility(event::AUTHENTICATION_FAILURE, LOG_AUTH);
    event::set_log_level(event::AUTHENTICATION_FAILURE, LOG_ERR);

    stringstream ss;
    ss << "test_event_";
    ss << getpid();
    ss << "_";

    for (int i = 0; i < 2; ++i)
    {
        ss << random();
    }

    string id = ss.str();

    MXS_LOG_EVENT(event::AUTHENTICATION_FAILURE, "%s", id.c_str());

    string name = get_auth_log();

    if (name.empty())
    {
        cout << "warning: Don't know where to look for authentication errors. Ignoring test." << endl;
    }
    else
    {
        if (access(name.c_str(), R_OK) != 0)
        {
            cout << "warning: Cannot read " << name << ", ignoring test." << endl;
            name.clear();
        }
    }

    if (!name.empty())
    {
        // We have no control over how quickly syslog messages are flushed
        // to the file. So, we try a few times before giving up.

        bool found = false;
        int attempts = 0;
        const int MAX_ATTEMPTS = 10;

        do
        {
            ++attempts;

            sleep(1);

            ifstream in(name);

            if (in)
            {
                string line;
                while (std::getline(in, line))
                {
                    if (line.find(id) != string::npos)
                    {
                        found = true;
                        cout << "notice: Found '" << id << "' in line '" << line << "'." << endl;
                    }
                }
            }
            else
            {
                cerr << "error: Could not open '" << name << "'." << endl;
                attempts = MAX_ATTEMPTS;
            }
        }
        while (!found && (attempts < MAX_ATTEMPTS));

        errors = found ? 0 : 1;
    }

    return errors;
}

}

int main()
{
    int errors = 0;

    srandom(time(NULL));

    if (mxs_log_init("TEST_EVENT", ".", MXS_LOG_TARGET_DEFAULT))
    {
        mxs_log_set_syslog_enabled(true);

        errors += test_levels();
        errors += test_facilities();
        errors += test_events();
        errors += test_logging();

        mxs_log_finish();
    }
    else
    {
        ++errors;
        cerr << "error: Could not initialize log manager." << endl;
    }

    return errors;
}
