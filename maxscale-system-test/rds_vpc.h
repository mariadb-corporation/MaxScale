#ifndef RDS_VPC_H
#define RDS_VPC_H

#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include <jansson.h>

using namespace std;

class RDS
{
public:
    /**
     * @brief RDS Constructor
     * @param cluster Name of cluster to create/destroy
     */
    RDS(char* cluster);

    const char* get_instance_name(json_t* instance);

    /**
     * @brief get_cluster Executes 'rds describe-bd-clusters' and creates json object with info on cluster
     * Finds cluster with ID 'cluster_name_intern'.
     * Does not set any internal variables
     * @return JSON describption of cluster
     */
    json_t* get_cluster();

    /**
     * @brief get_cluster_descr Creates JSON cluster describtion from string representation
     * @param json String representation of cluster description
     * Does not set any internal variables
     * @return JSON describption of cluster
     */
    json_t* get_cluster_descr(char* json);

    /**
     * @brief get_subnets_group_descr
     * @param json String representation of subnets grop description
     * Does not set any internal variables
     * @return JSON description of subnets group
     */
    json_t* get_subnets_group_descr(char* json);

    /**
     * @brief get_cluster_nodes Extract list of nodes names from cluster JSON description
     * Uses 'cluster_intern'
     * Does not set any internal variables
     * @return JSON array of node names strings
     */
    json_t* get_cluster_nodes();


    /**
     * @brief get_cluster_nodes Extract list of nodes names from cluster JSON description
     * Does not set any internal variables
     * @param cluster JSON cluster description
     * @return JSON array of nodes names strings
     */
    json_t* get_cluster_nodes(json_t* cluster);

    /**
     * @brief get_endpoints Gets list of endpoint (URLs) of cluster nodes
     * Sets 'cluster_intern'
     * @return JSON array of nodes endpoints (objects contaning Address and Port)
     */
    json_t* get_endpoints();

    /**
     * @brief get_subnets Extracts subnets IDs from subnets group
     * Uses 'subnets_group_name_intern'
     * Sets 'vpc_id_intern' and 'subnets_intern'
     * @return JSON array of node names strings
     */
    json_t* get_subnets();

    /**
     * @brief get_subnetgroup_name Extracts subnets grop ID from cluster description
     * Uses 'cluster_intern'
     * Sets 'subnets_group_name_intern'
     * If 'cluster_intern' is NULL function returns 'subnets_group_name_intern' value
     * @return name of subnets group
     */
    const char* get_subnetgroup_name();

    /**
     * @brief destroy_nodes Destroys nodes
     * @param node_names JSON array with nodes names
     * @return 0 in case of success
     */
    int destroy_nodes(json_t* node_names);

    /**
     * @brief destroy_subnets Destoys subnets
     * Uses 'subnets_intern' to get subnets list
     * If 'subnets_intern' is not set it is needed to run:
     *   - clustr_intern=get_cluster()
     *   - get_subnetgroup_name()
     *   - get_subnets()
     * @return 0 in case of success
     */
    int destroy_subnets();

    /**
     * @brief destroy_subnets_group Destroys subnets group
     * Uses 'subnets_group_name_intern'
     * If 'subnets_group_name_intern'  it is needed to run:
     *   - clustr_intern=get_cluster()
     *   - get_subnetgroup_name()
     * @return 0 in case of success
     */
    int destroy_subnets_group();

    /**
     * @brief destroy_route_tables Destroys route tabele
     * Not needed to executed directly, route table is destroyed by destroy_vpc
     * Uses 'vpc_id_intern'
     * If 'vpc_id_intern' is not set it is needed to run:
     *   - clustr_intern=get_cluster()
     *   - get_subnetgroup_name()
     *   - get_subnets()
     * @return  0 in case of success
     */
    int destroy_route_tables();     // is needed?

    /**
     * @brief destroy_vpc Destroys VPC
     * Uses 'vpc_id_intern'
     * if 'vpc_id_intern' is not set it is needed to run:
     *   - clustr_intern=get_cluster()
     *   - get_subnetgroup_name()
     *   - get_subnets()
     * @return 0 in case of success
     */
    int destroy_vpc();

    /**
     * @brief destroy_cluster Destroys RDS cluster
     * Uses 'cluster_name_intern'
     * @return 0 in case of success
     */
    int destroy_cluster();

    /**
     * @brief detach_and_destroy_gw Finds, detach and destroys internet gateways attached to VPC
     * Uses 'vpc_id_intern'
     * if 'vpc_id_intern' is not set it is needed to run:
     *   - clustr_intern=get_cluster()
     *   - get_subnetgroup_name()
     *   - get_subnets()
     * @return 0 in case of success
     */
    int detach_and_destroy_gw();

    /**
     * @brief create_vpc Creates VPC
     * Sets 'vpc_id_intern'
     * @param vpc_id Pointer to variable to place VpcID
     * @return 0 in case of success
     */
    int create_vpc(const char** vpc_id);

    /**
     * @brief create_subnet Creates subnet inside VPC
     * Adds element to 'subnets_intern' JSON array (creates it if it does not exist)
     * @param az Availability zone ID (e.g. 'eu-west-1a')
     * @param cidr CIDR block (e.g. '172.30.1.0/24')
     * @param subnet_id Pointer to variable to place SubnetID
     * @return 0 in case of success
     */
    int create_subnet(const char* az, const char* cidr, const char** subnet_id);

    /**
     * @brief create_subnet_group Creates subnets group for RDS
     * Uses 'subnets_intern'
     * Sets 'subnets_group_name_intern'
     * @return 0 in case of success
     */
    int create_subnet_group();

    /**
     * @brief create_gw Creates internet gateway for vpc_id_intern
     * Uses 'vpc_id_intern'
     * Sests 'gw_intern'
     * @param gw_id Pointer to variable to place gateway ID
     * @return 0 in case of success
     */
    int create_gw(const char** gw_id);

    /**
     * @brief configure_route_table Adds route to route tabele attched to VPC
     * Finds route table attached to VPC and adds route from internet to internet gateway
     * Uses 'vpc_id_intern' and 'gw_intern'
     * @param rt Pointer to variable to place route table ID which was found ond modified
     * @return 0 in case of success
     */
    int configure_route_table(const char** rt);

    /**
     * @brief create_cluster Creates RDS cluster and instances
     * Also configures security group (opens port 3306)
     * Uses 'cluster_name_intern', 'N_intern'
     * Sets cluster_intern, sg_intern
     * @return 0 in case of success
     */
    int create_cluster();

    /**
     * @brief get_writer Find instance which have 'write' attribute
     * Uses cluster_name_intern
     * Calls 'aws rds describe-db-clusters' and does not use 'cluster_intern'
     * (but does not update 'cluster_intern')
     * @param writer_name Pointer to variable to place name of writer node
     * @return 0 in case of success
     */
    int get_writer(const char** writer_name);

    /**
     * @brief create_rds_db Creates RDS DB cluster and all needed stuff (vpc, subnets, gateway, route table,
     *...)
     * If case of error tries to destry all created stuff
     * @param cluster_name Name of DB cluster
     * @param N Number of nodes
     * @return 0 in case if success
     */
    int create_rds_db(int N);

    /**
     * @brief delete_rds_cluster Destroys RDS cluster, instances and VPC in which RDS cluster was located
     * Uses 'cluster_name_intern'
     * Tries to get all items IDs
     * @return 0 in case if success
     */
    int delete_rds_cluster();

    /**
     * @brief wait_for_nodes Waits until N nodes are in 'avalable' state
     * Uses 'cluster_name_intern'
     * Sets 'cluster_intern'
     * @param N Number of nodes expected to be active (can be less than number of nodes in cluster)
     * @return 0 in case if success
     */
    int wait_for_nodes(size_t N);

    /**
     * @brief do_failover Does failover for RDS cluster
     * @return 0 in case if success
     */
    int do_failover();

    const char* cluster_name_intern;
    size_t      N_intern;
    json_t*     cluster_intern;
    const char* vpc_id_intern;
    json_t*     subnets_intern;
    const char* subnets_group_name_intern;
    const char* rt_intern;
    const char* gw_intern;
    const char* sg_intern;
};

#endif // RDS_VPC_H
