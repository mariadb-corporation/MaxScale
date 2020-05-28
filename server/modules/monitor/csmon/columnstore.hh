/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "csmon.hh"
#include <vector>
#include <maxbase/xml.hh>

namespace cs
{

const char ZCS_10[] = "1.0";
const char ZCS_12[] = "1.2";
const char ZCS_15[] = "1.5";

enum Version
{
    CS_UNKNOWN,
    CS_10,
    CS_12,
    CS_15
};

const char* to_string(Version version);

int get_minor_version(int full_version);

enum ClusterMode
{
    READONLY,
    READWRITE,
};

const char* to_string(ClusterMode cluster_mode);
bool from_string(const char* zCluster_mode, ClusterMode* pCluster_mode);

enum DbrmMode
{
    MASTER,
    SLAVE,
};

const char* to_string(DbrmMode dbrm_mode);
bool from_string(const char* zDbrm_mode, DbrmMode* pDbrm_mode);

using DbRootIdVector = std::vector<int>;
using ServiceVector = std::vector<std::pair<std::string,int>>;

bool from_string(const char* zTimestamp, std::chrono::system_clock::time_point* pTimestamp);
bool from_string(const char* zXml, std::unique_ptr<xmlDoc>* psDoc);
bool dbroots_from_array(json_t* pArray, DbRootIdVector* pDbroots);
bool services_from_array(json_t* pArray, ServiceVector* pServices);

namespace xml
{

const char CLUSTERMANAGER[]     = "ClusterManager";
const char DBRM_CONTROLLER[]    = "DBRM_Controller";
const char DBROOT[]             = "DBRoot";
const char DBROOTCOUNT[]        = "DBRootCount";
const char DDLPROC[]            = "DDLProc";
const char DMLPROC[]            = "DMLProc";
const char IPADDR[]             = "IPAddr";
const char MODULEIPADDR[]       = "ModuleIPAddr";
const char MODULEDBROOTCOUNT[]  = "ModuleDBRootCount";
const char MODULEDBROOTID[]     = "ModuleDBRootID";
const char SYSTEMCONFIG[]       = "SystemConfig";
const char SYSTEMMODULECONFIG[] = "SystemModuleConfig";

// In the config as various identifiers there is a trailing "X-Y-Z", where
// X is the node id, Y a sequence number and Z a number identifying the role
// of the entity identified by the identifier. The number "3" identifies a
// PM module, the only kind of node we are interested in.
const char ROLE_PM[] = "3";

/**
 * Find node id from Columnstore XML configuration.
 *
 * @param csXml    The XML document.
 * @param address  The IP address of the node.
 * @param pNid     On successful return, will contain the node id.
 *
 * @return True, if the node id was found, false otherwise.
 */
bool find_node_id(xmlDoc& csXml, const std::string& address, std::string* pNid);

/**
 * Update value of key(s) in Columnstore XML configuration.
 *
 * @param csXml       The XML document.
 * @param zXpath      The XML path that identifies the key(s).
 * @param zNew_value  The new value.
 * @param zIf_value   If non-NULL, what the previous value must be for the replacement to be done.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of replacements made.
 */
int update_if(xmlDoc& csXml, const char* zXpath, const char* zNew_value, const char* zIf_value);

/**
 * Update value of key(s) in Columnstore XML configuration.
 *
 * @param csXml       The XML document.
 * @param zXpath      The XML path that identifies the key(s).
 * @param zNew_value  The new value.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of replacements made.
 */
inline int update(xmlDoc& csXml, const char* zXpath, const char* zNew_value)
{
    return update_if(csXml, zXpath, zNew_value, nullptr);
}

/**
 * Update value of key(s) in Columnstore XML configuration.
 *
 * @param csXml       The XML document.
 * @param zXpath      The XML path that identifies the key(s).
 * @param zNew_value  The new value.
 * @param zIf_value   If non-NULL, what the previous value must *NOT* be for the replacement to be done.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of replacements made.
 */
int update_if_not(xmlDoc& csXml, const char* zXpath, const char* zNew_value, const char* zIf_value = nullptr);

/**
 * Insert new key/value to Columnstore XML configuration.
 *
 * @param csXml    The XML document.
 * @param zKey     The key. May be a path (not starting with '/') in which case
 *                 the hierarchy starting at @c pParent is first traversed.
 * @param zValue   The value.
 * @param location Where the element should be added.
 *
 * @return True, if the key/value could be added. A return value of false
 *         means that a path was specified, but the beginning path did not exist.
 */
bool insert(xmlDoc& csXml,
            const char* zKey,
            const char* zValue,
            mxb::xml::XmlLocation location = mxb::xml::XmlLocation::AT_BEGINNING);

/**
 * Update or insert a key/value to XML document.
 *
 * @param csXml    The XML document.
 * @param zKey     The XML path identifying the key.
 * @param zValue   The value.
 * @param location If inserted, where the element should be added.
 *
 * @return True, if the key/value could be updated or inserted. A return value
 *         of false means that the path did not identify an existing element
 *         and that the beginning path did not exist, so the key could not be
 *         added either.
 */
bool upsert(xmlDoc& csXml,
            const char* zKey,
            const char* zValue,
            mxb::xml::XmlLocation location = mxb::xml::XmlLocation::AT_BEGINNING);

/**
 * Remove key(s)
 *
 * @param csDoc    Columnstore XML configuration.
 * @param zXpath   The XML path identifying the key(s).
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of removed keys.
 */
int remove(xmlDoc& csXml, const char* zXPath);

inline std::string dump(const std::unique_ptr<xmlDoc>& sDoc)
{
    return mxb::xml::dump(*sDoc.get());
}

/**
 * @brief Convert single-node XML configuration to first multi-node configuration.
 *
 * This call will replace all occurences of "127.0.0.1" in the XML configuration
 * with the provided IP-address of the node and add a ClusterManager entry.
 *
 * @param csXml     Single-node configuration.
 * @param manager   The manager doing the modification.
 * @param address   The current public IP address of the node.
 */
void convert_to_first_multi_node(xmlDoc& csXml,
                                 const std::string& manager,
                                 const std::string& address);

/**
 * @brief Convert multi-node XML configuration to single-node configuration.
 *
 * This call will replace all occurences of the public IP address of the node with
 * "127.0.0.1" and remove the a ClusterManager entry.
 *
 * @param csXml  Multi-node configuration.
 */
void convert_to_single_node(xmlDoc& csXml);

namespace DbRoots
{

enum Status
{
    ERROR,
    NO_CHANGE,
    UPDATED
};

}

/**
 * @brief Update configuration dbroots.
 *
 * @param csXml    The Columnstore configuration.
 * @param address  The address of the server in question.
 * @param dbroots  The db roots of the server in question.
 * @param pOutput  Object where errors can be stored.
 *
 * @return What was done. If DbRoots::UPDATED, then @c csXml
 *         has been updated to reflect the reality.
 */
DbRoots::Status update_dbroots(xmlDoc& csXml,
                               const std::string& address,
                               const std::vector<int>& dbroots,
                               json_t* pOutput);

}

namespace rest
{
enum Action {
    BEGIN,
    COMMIT,
    CONFIG,
    ROLLBACK,
    SHUTDOWN,
    START,
    STATUS,
};

const char* to_string(Action action);

std::string create_url(const SERVER& server, int64_t port, const std::string& rest_base, Action action);

inline std::string create_url(const mxs::MonitorServer& mserver,
                              int64_t port,
                              const std::string& rest_base,
                              Action action)
{
    return create_url(*mserver.server, port, rest_base, action);
}

}

namespace body
{

const char CONFIG[]       = "config";
const char CLUSTER_MODE[] = "cluster_mode";
const char DBRM_MODE[]    = "dbrm_mode";
const char DBROOTS[]      = "dbroots";
const char ID[]           = "id";
const char MANAGER[]      = "manager";
const char MODE[]         = "mode";
const char NAME[]         = "name";
const char PID[]          = "pid";
const char REVISION[]     = "revision";
const char SERVICES[]     = "services";
const char TIMEOUT[]      = "timeout";
const char TIMESTAMP[]    = "timestamp";
const char TXN[]          = "txn";

/**
 * @brief JSON body to be used with PUT /node/begin
 *
 * @param timeout  The timeout.
 * @param id       The transaction id.
 *
 * @return REST-API body.
 */
std::string begin(const std::chrono::seconds& timeout, int id);

/**
 * @brief JSON body to be used with PUT /node/commit
 *
 * @param timeout  The timeout.
 * @param id       The transaction id.
 *
 * @return REST-API body.
 */
std::string commit(const std::chrono::seconds& timeout, int id);

/**
 * @brief JSON body to be used with PUT /node/config
 *
 * @param csXml     The Columnstore configuration.
 * @param revision  The revision of the configuration.
 * @param manager   The manager doing the modification.
 * @param timeout   The timeout.
 *
 * @return REST-API body.
 */
std::string config(const xmlDoc& csXml,
                   int revision,
                   const std::string& manager,
                   const std::chrono::seconds& timeout);

/**
 * @brief JSON body to be used with PUT /node/config
 *
 * @param mode     The cluster mode.
 * @param revision The revision of the configuration.
 * @param manager  The manager doing the modification.
 * @param timeout  The timeout.
 *
 * @return REST-API body.
 */
std::string config_set_cluster_mode(ClusterMode mode,
                                    int revision,
                                    const std::string& manager,
                                    const std::chrono::seconds& timeout);


/**
 * @brief JSON body to be used with PUT /node/rollback
 *
 * @param id  The transaction id.
 *
 * @return REST-API body.
 */
std::string rollback(int id);

/**
 * @brief JSON body to be used with PUT /node/shutdown
 *
 * @param timeout  The timeout.
 */
std::string shutdown(const std::chrono::seconds& timeout);

}

}
