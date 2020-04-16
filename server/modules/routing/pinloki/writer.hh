#pragma once

#include <maxbase/exception.hh>
#include "dbconnection.hh"
#include "gtid.hh"

#include <memory>

namespace pinloki
{
// TODO rename to?
class Writer
{
public:
    /**
     * @brief Writer
     */
    Writer();
    void run();
private:
    std::unique_ptr<maxsql::Connection> m_sConnection;
    bool                                m_is_bootstrap = false;
    maxsql::GtidList                    m_current_gtid_list;

    void save_gtid_list();
};
}
