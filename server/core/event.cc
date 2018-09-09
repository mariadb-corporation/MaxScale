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

#include "internal/event.hh"
#include <algorithm>
#include <string.h>
#include <maxbase/assert.h>
#include <maxbase/atomic.h>

using namespace std;

namespace
{

using namespace maxscale;

const char CN_UNKNOWN[] = "Unknown";

const char CN_FACILITY[] = "facility";
const char CN_LEVEL[] = "level";

const char CN_AUTHENTICATION_FAILURE[] = "authentication_failure";

const char EVENT_PREFIX[] = "event.";


struct NAME_AND_VALUE
{
    const char* zName;
    int32_t     value;
};

int name_and_value_compare(const void* pLeft, const void* pRight)
{
    const NAME_AND_VALUE* pL = static_cast<const NAME_AND_VALUE*>(pLeft);
    const NAME_AND_VALUE* pR = static_cast<const NAME_AND_VALUE*>(pRight);

    return strcmp(pL->zName, pR->zName);
}


// Keep these in alphabetical order.
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
};

const int N_FACILITIES = sizeof(facilities) / sizeof(facilities[0]);


struct EVENT
{
    const char* zName;
    event::id_t id;
    int32_t     facility;
    int32_t     level;
};

// Keep these in alphabetical order.
EVENT events[] =
{
    {
        CN_AUTHENTICATION_FAILURE,
        event::AUTHENTICATION_FAILURE,
        event::DEFAULT_FACILITY,
        event::DEFAULT_LEVEL
    }
};

const int N_EVENTS = sizeof(events) / sizeof(events[0]);

int event_compare(const void* pLeft, const void* pRight)
{
    const EVENT* pL = static_cast<const EVENT*>(pLeft);
    const EVENT* pR = static_cast<const EVENT*>(pRight);

    return strcmp(pL->zName, pR->zName);
}


struct
{
    EVENT*                events;
    const NAME_AND_VALUE* levels;
    const NAME_AND_VALUE* facilities;
} this_unit =
{
    events,
    levels,
    facilities
};

event::result_t configure_facility(event::id_t id, const char* zValue)
{
    event::result_t rv = event::INVALID;

    int32_t facility;
    if (log_facility_from_string(&facility, zValue))
    {
        event::set_log_facility(id, facility);
        rv = event::ACCEPTED;
    }
    else
    {
        MXS_ERROR("%s is not a valid facility.", zValue);
    }

    return rv;
}

event::result_t configure_level(event::id_t id, const char* zValue)
{
    event::result_t rv = event::INVALID;

    int32_t level;
    if (log_level_from_string(&level, zValue))
    {
        event::set_log_level(id, level);
        rv = event::ACCEPTED;
    }
    else
    {
        MXS_ERROR("%s is not a valid level.", zValue);
    }

    return rv;
}
}

namespace maxscale
{

const char* log_level_to_string(int32_t level)
{
    auto begin = this_unit.levels;
    auto end = begin + N_LEVELS;

    auto i = find_if(begin,
                     end,
                     [level](const NAME_AND_VALUE& item) -> bool {
                         return item.value == level;
                     });

    return i == end ? CN_UNKNOWN : i->zName;
}

bool log_level_from_string(int32_t* pLevel, const char* zValue)
{
    NAME_AND_VALUE key = {zValue};
    void* pResult = bsearch(&key,
                            this_unit.levels,
                            N_LEVELS,
                            sizeof(NAME_AND_VALUE),
                            name_and_value_compare);

    if (pResult)
    {
        const NAME_AND_VALUE* pItem = static_cast<const NAME_AND_VALUE*>(pResult);

        *pLevel = pItem->value;
    }

    return pResult != nullptr;
}

const char* log_facility_to_string(int32_t facility)
{
    auto begin = this_unit.facilities;
    auto end = begin + N_FACILITIES;

    auto i = find_if(begin,
                     end,
                     [facility](const NAME_AND_VALUE& item) -> bool {
                         return item.value == facility;
                     });

    return i == end ? CN_UNKNOWN : i->zName;
}

bool log_facility_from_string(int32_t* pFacility, const char* zValue)
{
    NAME_AND_VALUE key = {zValue};
    void* pResult = bsearch(&key,
                            this_unit.facilities,
                            N_FACILITIES,
                            sizeof(NAME_AND_VALUE),
                            name_and_value_compare);

    if (pResult)
    {
        const NAME_AND_VALUE* pItem = static_cast<const NAME_AND_VALUE*>(pResult);

        *pFacility = pItem->value;
    }

    return pResult != nullptr;
}


namespace event
{

const char* to_string(id_t id)
{
    auto begin = this_unit.events;
    auto end = begin + N_EVENTS;

    auto i = find_if(begin,
                     end,
                     [id](const EVENT& item) -> bool {
                         return item.id == id;
                     });

    return i == end ? CN_UNKNOWN : i->zName;
}

bool from_string(id_t* pId, const char* zValue)
{
    EVENT key = {zValue};
    void* pResult = bsearch(&key,
                            this_unit.events,
                            N_EVENTS,
                            sizeof(EVENT),
                            event_compare);

    if (pResult)
    {
        const EVENT* pItem = static_cast<const EVENT*>(pResult);

        *pId = pItem->id;
    }

    return pResult != nullptr;
}

void set_log_facility(id_t id, int32_t facility)
{
    bool rv = false;
    mxb_assert((id >= 0) && (id < N_EVENTS));

    // We silently strip away other than the relevant bits.
    facility = facility & LOG_FACMASK;

    EVENT& event = this_unit.events[id];

    atomic_store_int32(&event.facility, facility);
}

int32_t get_log_facility(id_t id)
{
    mxb_assert((id >= 0) && (id < N_EVENTS));

    const EVENT& event = this_unit.events[id];

    return atomic_load_int32(&event.facility);
}

void set_log_level(id_t id, int32_t level)
{
    mxb_assert((id >= 0) && (id < N_EVENTS));

    // We silently strip away other than the relevant bits.
    level = level & LOG_PRIMASK;

    EVENT& event = this_unit.events[id];

    atomic_store_int32(&event.level, level);
}

int32_t get_log_level(id_t id)
{
    mxb_assert((id >= 0) && (id < N_EVENTS));

    const EVENT& event = this_unit.events[id];

    return atomic_load_int32(&event.level);
}

result_t configure(const char* zName, const char* zValue)
{
    result_t rv = IGNORED;

    if (strncmp(zName, EVENT_PREFIX, sizeof(EVENT_PREFIX) - 1) == 0)
    {
        rv = INVALID;

        string name(zName + sizeof(EVENT_PREFIX) - 1);      // Character following '.'

        auto i = name.find_first_of('.');

        if (i != string::npos)
        {
            string event = name.substr(0, i);
            string property = name.substr(i + 1);

            id_t id;
            if (from_string(&id, event.c_str()))
            {
                mxb_assert((id >= 0) && (id < N_EVENTS));

                if (property == CN_FACILITY)
                {
                    rv = configure_facility(id, zValue);
                }
                else if (property == CN_LEVEL)
                {
                    rv = configure_level(id, zValue);
                }
                else
                {
                    MXS_ERROR("%s is neither %s nor %s.", property.c_str(), CN_FACILITY, CN_LEVEL);
                }
            }
            else
            {
                MXS_ERROR("%s does not refer to a known event.", zValue);
            }
        }
        else
        {
            MXS_ERROR("%s is not a valid event configuration.", zName);
        }
    }

    return rv;
}

void log(id_t event_id,
         const char* modname,
         const char* file,
         int line,
         const char* function,
         const char* format,
         ...)
{
    va_list valist;

    mxb_assert((event_id >= 0) && (event_id < N_EVENTS));

    const EVENT& event = this_unit.events[event_id];

    int priority = atomic_load_int32(&event.facility) | atomic_load_int32(&event.level);

    va_start(valist, format);
    int len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    if (len > BUFSIZ)
    {
        len = BUFSIZ;
    }

    char message[len + 1];

    va_start(valist, format);
    vsnprintf(message, len + 1, format, valist);
    va_end(valist);

    mxs_log_message(priority, modname, file, line, function, "%s", message);
}
}   // event
}   // maxscale
