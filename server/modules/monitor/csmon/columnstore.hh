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

using DbRoots = std::vector<int>;
using Services = std::vector<std::pair<std::string,int>>;

bool from_string(const char* zTimestamp, std::chrono::system_clock::time_point* pTimestamp);
bool from_string(const char* zXml, std::unique_ptr<xmlDoc>* psDoc);
bool dbroots_from_array(json_t* pArray, DbRoots* pDbroots);
bool services_from_array(json_t* pArray, Services* pServices);

namespace xml
{

const char CLUSTERMANAGER[]  = "ClusterManager";
const char DBRM_CONTROLLER[] = "DBRM_Controller";
const char DDLPROC[]         = "DDLProc";
const char DMLPROC[]         = "DMLProc";
const char IPADDR[]          = "IPAddr";

const char XPATH_CLUSTERMANAGER[] = "//ClusterManager";
const char XPATH_IPADDR[]         = "//IPAddr";

/**
 * Find nodes in document corresponding to particular xpath.
 *
 * @param xmlDoc  The XML document.
 * @param zXpath  The xpath.
 *
 * @return The nodes corresponding to the xpath.
 */
std::vector<xmlNode*> find_nodes_by_xpath(xmlDoc& xml, const char* zXpath);
/**
 * Find node in document corresponding to particular xpath.
 *
 * @note Should only be used with an xpath that can only identify a single.
 *
 * @param xmlDoc  The XML document.
 * @param zXpath  The xpath.
 *
 * @return The node corresponding to the xpath, or NULL if none do.
 */
xmlNode* find_node_by_xpath(xmlDoc& xml, const char* zXpath);

/**
 * Find children whose names begins with a certain prefix.
 *
 * @param parent   The parent node.
 * @param zPrefix  The prefix the name of a child should have to be included.
 *
 * @return Children that fulfill the requirement.
 */
std::vector<xmlNode*> find_children_by_prefix(xmlNode& parent, const char* zPrefix);

/**
 * Find node id from Columnstore XML configuration.
 *
 * @param xmlDoc   The XML document.
 * @param address  The IP address of the node.
 * @param pNid     On successful return, will contain the node id.
 *
 * @return True, if the node id was found, false otherwise.
 */
bool find_node_id(xmlDoc& xmlDoc, const std::string& address, std::string* pNid);

/**
 * Update value of key(s) in XML document.
 *
 * @param xmlDoc      The XML document.
 * @param zXpath      The XML path that identifies the key(s).
 * @param zNew_value  The new value.
 * @param zIf_value   If non-NULL, what the previous value must be for the replacement to be done.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of replacements made.
 */
int update_if(xmlDoc& xmlDoc, const char* zXpath, const char* zNew_value, const char* zIf_value = nullptr);

/**
 * Update value of key(s) in XML document.
 *
 * @param xmlDoc      The XML document.
 * @param zXpath      The XML path that identifies the key(s).
 * @param zNew_value  The new value.
 * @param zIf_value   If non-NULL, what the previous value must *NOT* be for the replacement to be done.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of replacements made.
 */
int update_if_not(xmlDoc& xmlDoc, const char* zXpath, const char* zNew_value, const char* zIf_value = nullptr);

enum class XmlLocation
{
    AT_BEGINNING,
    AT_END
};

/**
 * Insert new key/value to XML document.
 *
 * @param xmlDoc   The XML document.
 * @param zKey     The key.
 * @param zValue   The value.
 * @param location Where the element should be added.
 *
 * @return True, whether the value could be added.
 */
bool insert(xmlDoc& xmlDoc,
            const char* zKey,
            const char* zValue,
            XmlLocation location = XmlLocation::AT_BEGINNING);

/**
 * Update or insert a key/value to XML document.
 *
 * @param xmlDoc   The XML document.
 * @param zXpath   The XML path identifying the key(s).
 * @param zValue   The value.
 * @param location If inserted, where the element should be added.
 *
 * If an existing key is not found, the new key is deduced from @c zXpath.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of updates/inserts made.
 */
int upsert(xmlDoc& xmlDoc,
           const char* zXpath,
           const char* zValue,
           XmlLocation location = XmlLocation::AT_BEGINNING);

/**
 * Remove key(s)
 *
 * @param xmlDoc   The XML document.
 * @param zXpath   The XML path identifying the key(s).
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of removed keys.
 */
int remove(xmlDoc& xmlDoc, const char* zXPath);

/**
 * @brief Convert single-node XML configuration to first multi-node configuration.
 *
 * This call will replace all occurences of "127.0.0.1" in the XML configuration
 * with the provided IP-address of the node and add a ClusterManager entry.
 *
 * @param xmlDoc    Single-node configuration.
 * @param manager   The manager doing the modification.
 * @param address   The current public IP address of the node.
 */
void convert_to_first_multi_node(xmlDoc& xmlDoc,
                                 const std::string& manager,
                                 const std::string& address);

/**
 * @brief Convert multi-node XML configuration to single-node configuration.
 *
 * This call will replace all occurences of the public IP address of the node with
 * "127.0.0.1" and remove the a ClusterManager entry.
 *
 * @param xmlDoc    Multi-node configuration.
 */
void convert_to_single_node(xmlDoc& xmlDoc);

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
 * @param id       The tranaction id.
 *
 * @return REST-API body.
 */
std::string begin(const std::chrono::seconds& timeout, int id);

/**
 * @brief JSON body to be used with PUT /node/config
 *
 * This call will replace all occurences of "127.0.0.1" in the XML configuration
 * with the public IP-address of the node and add a ClusterManager entry. This is
 * to be used when the very first (currently single mode) node is added to the
 * cluster.
 *
 * @param xmlDoc    The original Columnstore configuration of the node.
 *                  NOTE: Will be modified as a result of the call.
 * @param revision  The revision of the configuration.
 * @param manager   The manager doing the modification.
 * @param address   The current public IP address of the node.
 * @param timeout   The timeout.
 *
 * @return REST-API body.
 */
std::string config_first_multi_node(xmlDoc& xmlDoc,
                                    int revision,
                                    const std::string& manager,
                                    const std::string& address,
                                    const std::chrono::seconds& timeout);

/**
 * @brief JSON body to be used with PUT /node/config
 *
 * This call will replace all occurences of the public IP address of the node with
 * "127.0.0.1" and remove the a ClusterManager entry. This is to be used when a
 * node is removed from the cluster.
 *
 * @param xmlDoc    The original Columnstore configuration of the node.
 *                  NOTE: Will be modified as a result of the call.
 * @param revision  The revision of the configuration.
 * @param manager   The manager doing the modification.
 * @param timeout   The timeout.
 *
 * @return REST-API body.
 */
std::string config_reset_node(xmlDoc& xmlDoc,
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
 * @brief JSON body to be used with PUT /node/shutdown
 *
 * @param timeout  The timeout.
 */
std::string shutdown(const std::chrono::seconds& timeout);

}

}
