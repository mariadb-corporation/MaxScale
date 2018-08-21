/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

#include <maxbase/ccdefs.hh>
#include <stdexcept>
#include <maxbase/log.h>
#include <maxbase/maxbase.h>

namespace maxbase
{

/**
 * @brief Initializes the maxbase library
 *
 * This function should be called before any other functionality of
 * maxbase is used. A notable exception is the maxbase log that can
 * be initialized and used independently.
 *
 * Note that if an instance of @c MaxBase is created, it will call
 * both @init and @finish.
 *
 * @return True, if maxbase could be initialized, false otherwise.
 */
bool init();

/**
 * @brief Finalizes the maxbase library
 *
 * This function should be called before program exit, if @c init()
 * returned true.
 */
void finish();


/**
 * @class MaxBase
 *
 * A simple utility RAII class where the constructor initializes maxbase
 * (and optionally the log) and the destructor finalizes it.
 */
class MaxBase
{
    MaxBase(const MaxBase&) = delete;
    MaxBase& operator=(const MaxBase&) = delete;

public:
    /**
     * @brief Initializes MaxBase but not the MaxBase log.
     */
    MaxBase()
        : m_log_inited(false)
    {
        if (!maxbase_init())
        {
            throw std::runtime_error("Initialization of maxbase failed.");
        }
    }

    /**
     * @brief Initializes MaxBase and the MaxBase log.
     *
     * @see mxb_log_init
     *
     * @throws std::runtime_error if the initialization failed.
     */
    MaxBase(const char* zIdent,
            const char* zLogdir,
            const char* zFilename,
            mxb_log_target_t target,
            mxb_log_context_provider_t context_provider);

    /**
     * @brief Initializes MaxBase and the MaxBase log.
     *
     * @see mxb_log_init
     *
     * @throws std::runtime_error if the initialization failed.
     */
    MaxBase(mxb_log_target_t target)
        : MaxBase(nullptr, ".", nullptr, target, nullptr)
    {
    }

    ~MaxBase()
    {
        if (m_log_inited)
        {
            mxb_log_finish();
        }

        maxbase::finish();
    }

private:
    bool m_log_inited;
};

}
