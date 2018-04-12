#pragma once

/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "mariadbmon_common.hh"
#include <string>
#include <vector>

/**
 * Class which encapsulates a gtid (one domain-server_id-sequence combination)
 */
class Gtid
{
public:

    /**
     * Constructs an invalid Gtid.
     */
    Gtid();

    /**
     * Constructs a gtid with given values. The values are not checked.
     *
     * @param domain Domain
     * @param server_id Server id
     * @param sequence Sequence
     */
    Gtid(uint32_t domain, int64_t server_id, uint64_t sequence);

    /**
     * Parse one gtid from null-terminated string. Handles multi-domain gtid:s properly. Should be called
     * repeatedly for a multi-domain gtid string by giving the value of @c endptr as @c str.
     *
     * @param str First number of a gtid in a gtid-string
     * @param endptr A pointer to save the position at after the last parsed character.
     * @return A new gtid. If an error occurs, the server_id of the returned triplet is -1.
     */
    static Gtid from_string(const char* str, char** endptr);

    bool eq(const Gtid& rhs) const;

    std::string to_string() const;

    /**
     * Comparator, used when sorting by domain id.
     *
     * @param lhs Left side
     * @param rhs Right side
     * @return True if lhs should be before rhs
     */
    static bool compare_domains(const Gtid& lhs, const Gtid& rhs)
    {
        return lhs.m_domain < rhs.m_domain;
    }

    uint32_t m_domain;
    int64_t m_server_id; // Valid values are 32bit unsigned. 0 is only used by server versions  <= 10.1
    uint64_t m_sequence;
};

inline bool operator == (const Gtid& lhs, const Gtid& rhs)
{
    return lhs.eq(rhs);
}

/**
 * Class which encapsulates a list of gtid:s (e.g. 1-2-3,2-2-4). Server variables such as gtid_binlog_pos
 * are GtidLists. */
class GtidList
{
public:

    // Used with events_ahead()
    enum substraction_mode_t
    {
        MISSING_DOMAIN_IGNORE,
        MISSING_DOMAIN_LHS_ADD
    };

    /**
     * Parse the gtid string and return an object. Orders the triplets by domain id.
     *
     * @param gtid_string gtid as given by server. String must not be empty.
     * @return The parsed (possibly multidomain) gtid. In case of error, the gtid will be empty.
     */
    static GtidList from_string(const std::string& gtid_string);

    /**
     * Return a string version of the gtid list.
     *
     * @return A string similar in form to how the server displays gtid:s
     */
    std::string to_string() const;

    /**
     * Check if a server with this gtid can replicate from a master with a given gtid. Only considers
     * gtid:s and only detects obvious errors. The non-detected errors will mostly be detected once
     * the slave tries to start replicating.
     *
     * TODO: Add support for Replicate_Do/Ignore_Id:s
     *
     * @param master_gtid Master server gtid
     * @return True if replication looks possible
     */
    bool can_replicate_from(const GtidList& master_gtid);

    /**
     * Is the gtid empty.
     *
     * @return True if gtid has 0 triplets
     */
    bool empty() const;

    /**
     * Full comparison.
     *
     * @param rhs Other gtid
     * @return True if both gtid:s have identical triplets or both are empty
     */
    bool operator == (const GtidList& rhs) const;

    /**
     * Calculate the number of events between two gtid:s with possibly multiple triplets. The
     * result is always 0 or greater: if a sequence number of a domain on rhs is greater than on the same
     * domain on lhs, the sequences are considered identical. Missing domains are handled depending on the
     * value of @c domain_substraction_mode.
     *
     * @param lhs The value substracted from
     * @param io_pos The value doing the substracting
     * @param domain_substraction_mode How domains that exist on one side but not the other are handled. If
     * MISSING_DOMAIN_IGNORE, these are simply ignored. If MISSING_DOMAIN_LHS_ADD, the sequence number on lhs
     * is added to the total difference.
     * @return The number of events between the two gtid:s
     */
    static uint64_t events_ahead(const GtidList& lhs, const GtidList& rhs,
                                 substraction_mode_t domain_substraction_mode);

    /**
     * Return an individual gtid with the given domain.
     *
     * @param domain Which domain to search for
     * @return The gtid within the list. If domain is not found, an invalid gtid is returned.
     */
    Gtid get_gtid(uint32_t domain) const;

private:
    std::vector<Gtid> m_triplets;
};