#include "maria_rpl_event.hh"
#include "dbconnection.hh"

#include <chrono>
#include <iostream>
#include <iomanip>

using namespace std::literals::chrono_literals;
using namespace std::literals::string_literals;

namespace maxsql
{
MariaRplEvent::MariaRplEvent(st_mariadb_rpl_event* pEvent, st_mariadb_rpl* handle)
    : m_pEvent {pEvent}
    , m_pRpl_handle {handle}
{
    // There's a bug in mariadb_rpl in reading the checksum
    // m_pEvent->checksum = *((uint32_t*)(m_pRpl_handle->buffer + m_pRpl_handle->buffer_size - 4));
}

const st_mariadb_rpl_event& MariaRplEvent::event() const
{
    return *m_pEvent;
}

const char* MariaRplEvent::raw_data() const
{
    // discard the extra byte in the event buffer
    return reinterpret_cast<const char*>(m_pRpl_handle->buffer) + 1;
}

size_t MariaRplEvent::raw_data_size() const
{
    // discard the extra byte in the event buffer
    return m_pRpl_handle->buffer_size - 1;
}

maxsql::MariaRplEvent::~MariaRplEvent()
{
    mariadb_free_rpl_event(m_pEvent);
}

std::ostream& operator<<(std::ostream& os, const MariaRplEvent& rpl_msg)
{
    os << dump_rpl_msg(rpl_msg, Verbosity::All);
    return os;
}


std::string dump_rpl_msg(const MariaRplEvent& rpl_msg, Verbosity v)
{
    std::ostringstream oss;
    auto& rpl_event = rpl_msg.event();
    bool is_artificial = rpl_event.flags & LOG_EVENT_ARTIFICIAL_F;

    oss << to_string(rpl_event.event_type) << '\n';
    switch (rpl_event.event_type)
    {
    case ROTATE_EVENT:
        {
            auto& ev = rpl_event.event.rotate;
            // TODO rotate.filename.length is incorrect
            oss << "  rotate file " << std::string(ev.filename.str, 13) << '\n';
        }
        break;

    case ANNOTATE_ROWS_EVENT:
        {
            auto& ev = rpl_event.event.annotate_rows;
            oss << "  annotation " << std::string(ev.statement.str, ev.statement.length) << '\n';
        }
        break;

    case FORMAT_DESCRIPTION_EVENT:
        {
            auto& ev = rpl_event.event.format_description;
            oss << "  " << ev.server_version << '\n';
        }
        break;

    case GTID_LIST_EVENT:
        {
            auto& ev = rpl_event.event.gtid_list;
            // There's a bug in mariadb_rpl. In GTID_LIST_EVENT deserialize, after the
            // count the ptr is ev++, should be ev+=4
            const char* dptr = rpl_msg.raw_data();
            int count = *((uint32_t*) dptr);
            oss << "count=" << ev.gtid_cnt << '\n';
            dptr += 4;

            for (uint32_t i = 0; i < ev.gtid_cnt; ++i)
            {
                oss << "D " << ev.gtid[i].domain_id << " S " << ev.gtid[i].server_id << " Q "
                    << ev.gtid[i].sequence_nr << '\n';
            }
        }
        break;

    case GTID_EVENT:
        {
            auto& ev = rpl_event.event.gtid;
            maxsql::Gtid gtid = maxsql::Gtid(ev.domain_id, rpl_event.server_id, ev.sequence_nr);
            oss << "  " << gtid << '\n';
        }
        break;

    default:
        // pass
        break;
    }

    if (v == Verbosity::All)
    {
        oss << "ok " << std::boolalpha << bool(rpl_event.ok) << "\n";
        oss << "is_artificial  = " << std::boolalpha << bool(is_artificial) << "\n";
        oss << "event_type     = " << rpl_event.event_type << "\n";
        oss << "event_length   = " << rpl_event.event_length << "\n";
        oss << "start_position = " << rpl_msg.rpl_hndl().start_position << "\n";
        oss << "buffer_size    = " << rpl_msg.rpl_hndl().buffer_size << "\n";
        oss << "fd_header_len  = " << int(rpl_msg.rpl_hndl().fd_header_len) << "\n";
        oss << "server_id      = " << rpl_event.server_id << "\n";
        oss << "next_event_pos = " << rpl_event.next_event_pos << std::endl;
        oss << "use_checksum   = " << std::boolalpha << (bool)rpl_msg.rpl_hndl().use_checksum << std::endl;
        oss << "checksum       = " << std::hex << "0x" << rpl_event.checksum << std::endl;
    }

    return oss.str();
}
}

std::string to_string(mariadb_rpl_event ev)
{
    switch (ev)
    {
    case START_EVENT_V3:
        return "START_EVENT_V3";

    case QUERY_EVENT:
        return "QUERY_EVENT";

    case STOP_EVENT:
        return "STOP_EVENT";

    case ROTATE_EVENT:
        return "ROTATE_EVENT";

    case INTVAR_EVENT:
        return "INTVAR_EVENT";

    case LOAD_EVENT:
        return "LOAD_EVENT";

    case SLAVE_EVENT:
        return "SLAVE_EVENT";

    case CREATE_FILE_EVENT:
        return "CREATE_FILE_EVENT";

    case APPEND_BLOCK_EVENT:
        return "APPEND_BLOCK_EVENT";

    case EXEC_LOAD_EVENT:
        return "EXEC_LOAD_EVENT";

    case DELETE_FILE_EVENT:
        return "DELETE_FILE_EVENT";

    case NEW_LOAD_EVENT:
        return "NEW_LOAD_EVENT";

    case RAND_EVENT:
        return "RAND_EVENT";

    case USER_VAR_EVENT:
        return "USER_VAR_EVENT";

    case FORMAT_DESCRIPTION_EVENT:
        return "FORMAT_DESCRIPTION_EVENT";

    case XID_EVENT:
        return "XID_EVENT";

    case BEGIN_LOAD_QUERY_EVENT:
        return "BEGIN_LOAD_QUERY_EVENT";

    case EXECUTE_LOAD_QUERY_EVENT:
        return "EXECUTE_LOAD_QUERY_EVENT";

    case TABLE_MAP_EVENT:
        return "TABLE_MAP_EVENT";

    case PRE_GA_WRITE_ROWS_EVENT:
        return "PRE_GA_WRITE_ROWS_EVENT";

    case PRE_GA_UPDATE_ROWS_EVENT:
        return "PRE_GA_UPDATE_ROWS_EVENT";

    case PRE_GA_DELETE_ROWS_EVENT:
        return "PRE_GA_DELETE_ROWS_EVENT";

    case WRITE_ROWS_EVENT_V1:
        return "WRITE_ROWS_EVENT_V1";

    case UPDATE_ROWS_EVENT_V1:
        return "UPDATE_ROWS_EVENT_V1";

    case DELETE_ROWS_EVENT_V1:
        return "DELETE_ROWS_EVENT_V1";

    case INCIDENT_EVENT:
        return "INCIDENT_EVENT";

    case HEARTBEAT_LOG_EVENT:
        return "HEARTBEAT_LOG_EVENT";

    case IGNORABLE_LOG_EVENT:
        return "IGNORABLE_LOG_EVENT";

    case ROWS_QUERY_LOG_EVENT:
        return "ROWS_QUERY_LOG_EVENT";

    case WRITE_ROWS_EVENT:
        return "WRITE_ROWS_EVENT";

    case UPDATE_ROWS_EVENT:
        return "UPDATE_ROWS_EVENT";

    case DELETE_ROWS_EVENT:
        return "DELETE_ROWS_EVENT";

    case GTID_LOG_EVENT:
        return "GTID_LOG_EVENT";

    case ANONYMOUS_GTID_LOG_EVENT:
        return "ANONYMOUS_GTID_LOG_EVENT";

    case PREVIOUS_GTIDS_LOG_EVENT:
        return "PREVIOUS_GTIDS_LOG_EVENT";

    case TRANSACTION_CONTEXT_EVENT:
        return "TRANSACTION_CONTEXT_EVENT";

    case VIEW_CHANGE_EVENT:
        return "VIEW_CHANGE_EVENT";

    case XA_PREPARE_LOG_EVENT:
        return "XA_PREPARE_LOG_EVENT";

    case ANNOTATE_ROWS_EVENT:
        return "ANNOTATE_ROWS_EVENT";

    case BINLOG_CHECKPOINT_EVENT:
        return "BINLOG_CHECKPOINT_EVENT";

    case GTID_EVENT:
        return "GTID_EVENT";

    case GTID_LIST_EVENT:
        return "GTID_LIST_EVENT";

    case START_ENCRYPTION_EVENT:
        return "START_ENCRYPTION_EVENT";

    case QUERY_COMPRESSED_EVENT:
        return "QUERY_COMPRESSED_EVENT";

    case WRITE_ROWS_COMPRESSED_EVENT_V1:
        return "WRITE_ROWS_COMPRESSED_EVENT_V1";

    case UPDATE_ROWS_COMPRESSED_EVENT_V1:
        return "UPDATE_ROWS_COMPRESSED_EVENT_V1";

    case DELETE_ROWS_COMPRESSED_EVENT_V1:
        return "DELETE_ROWS_COMPRESSED_EVENT_V1";

    case WRITE_ROWS_COMPRESSED_EVENT:
        return "WRITE_ROWS_COMPRESSED_EVENT";

    case UPDATE_ROWS_COMPRESSED_EVENT:
        return "UPDATE_ROWS_COMPRESSED_EVENT";

    case DELETE_ROWS_COMPRESSED_EVENT:
        return "DELETE_ROWS_COMPRESSED_EVENT";

    default:
        return "UNKNOWN_EVENT";
    }

    abort();
}
