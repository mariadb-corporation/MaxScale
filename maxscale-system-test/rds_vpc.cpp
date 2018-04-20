#include "execute_cmd.h"
#include "rds_vpc.h"

RDS::RDS(char * cluster)
{
    cluster_name_intern = cluster;
    subnets_intern = NULL;
    N_intern = 0;
}

const char * RDS::get_instance_name(json_t * instance)
{
    json_t * instance_name = json_object_get(instance, "DBInstanceIdentifier");
    return json_string_value(instance_name);
}

json_t * RDS::get_cluster_descr(char * json)
{
    json_t *root;
    json_error_t error;

    root = json_loads( json, 0, &error );
    if ( !root )
    {
        fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
        return NULL;
    }

    json_t * clusters = json_object_get(root, "DBClusters");
    //cluster_intern =
    return json_array_get(clusters, 0);
}

json_t * RDS::get_subnets_group_descr(char * json)
{
    json_t *root;
    json_error_t error;

    root = json_loads( json, 0, &error );
    if ( !root )
    {
        fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
        return NULL;
    }

    json_t * subnets = json_object_get(root, "DBSubnetGroups");
    return json_array_get(subnets, 0);
}

json_t * RDS::get_cluster_nodes()
{
    return get_cluster_nodes(cluster_intern);
}

json_t * RDS::get_cluster_nodes(json_t *cluster)
{
    json_t * members = json_object_get(cluster, "DBClusterMembers");
    size_t members_N = json_array_size(members);
    json_t * member;
    json_t * node_names = json_array();

    for (size_t i = 0; i < members_N; i++)
    {
        member = json_array_get(members, i);
        json_array_append(node_names, json_string(get_instance_name(member)));
    }
    return node_names;
}

json_t * RDS::get_subnets()
{
    char cmd[1024];
    char *result;
    sprintf(cmd, "aws rds describe-db-subnet-groups --db-subnet-group-name %s", subnets_group_name_intern);
    if (execute_cmd(cmd, &result) != 0)
    {
        return NULL;
    }

    json_t * subnets_group = get_subnets_group_descr(result);

    json_t * members = json_object_get(subnets_group, "Subnets");
    vpc_id_intern = json_string_value(json_object_get(subnets_group, "VpcId"));
    size_t members_N = json_array_size(members);
    json_t * member;
    json_t * subnets_names = json_array();

    for (size_t i = 0; i < members_N; i++)
    {
        member = json_array_get(members, i);
        json_array_append(subnets_names, json_object_get(member, "SubnetIdentifier"));
    }
    subnets_intern = subnets_names;
    return subnets_names;
}

const char * RDS::get_subnetgroup_name()
{
    if (cluster_intern != NULL)
    {
        subnets_group_name_intern = json_string_value(json_object_get(cluster_intern, "DBSubnetGroup"));
    }
    else
    {
        subnets_group_name_intern = cluster_name_intern;
    }

    return subnets_group_name_intern;
}

json_t * RDS::get_cluster()
{
    char cmd[1024];
    char *result;
    sprintf(cmd, "aws rds describe-db-clusters --db-cluster-identifier=%s", cluster_name_intern);
    execute_cmd(cmd , &result);
    return get_cluster_descr(result);
}

int RDS::destroy_nodes(json_t * node_names)
{
    size_t N = json_array_size(node_names);

    char cmd[1024];
    char *res;
    json_t * node;
    int err = 0;
    for (size_t i = 0; i < N; i++)
    {
        node = json_array_get(node_names, i);
        sprintf(cmd, "aws rds delete-db-instance --skip-final-snapshot --db-instance-identifier=%s",
                json_string_value(node));
        printf("%s\n", cmd);
        if (execute_cmd(cmd, &res) != 0)
        {
            err = -1;
            fprintf( stderr, "error: can not delete node %s\n", json_string_value(node));
        }
    }
    return err;
}

int RDS::destroy_subnets()
{
    size_t N = json_array_size(subnets_intern);

    char cmd[1024];
    char *res;
    json_t * subnet;
    int err = 0;
    for (size_t i = 0; i < N; i++)
    {
        subnet = json_array_get(subnets_intern, i);
        sprintf(cmd, "aws ec2 delete-subnet --subnet-id=%s", json_string_value(subnet));
        printf("%s\n", cmd);
        execute_cmd(cmd, &res);
        if (execute_cmd(cmd, &res) != 0)
        {
            err = -1;
            fprintf( stderr, "error: can not delete subnet %s\n", json_string_value(subnet));
        }
    }
    return err;
}

int RDS::destroy_route_tables()
{
    json_t *root;
    char cmd[1024];
    char * json;

    sprintf(cmd, "aws ec2 describe-vpcs --vpc-ids=%s", vpc_id_intern);
    if (execute_cmd(cmd, &json))
    {
        fprintf( stderr, "error: can not get internet gateways description\n");
        return -1;
    }

    root = get_cluster_descr(json);
    if ( !root )
    {
        fprintf( stderr, "error: can not get cluster description\n");
        return -1;
    }

    json_t * route_tables = json_object_get(root, "RouteTables");

    size_t i;
    json_t *route_table;
    const char * rt_id;
    const char * vpc_id;
    json_array_foreach(route_tables, i, route_table)
    {
        rt_id = json_string_value(json_object_get(route_table, "RouteTableId"));
        vpc_id = json_string_value(json_object_get(route_table, "VpcId"));
        if (strcmp(vpc_id_intern, vpc_id) == 0)
        {
            sprintf(cmd, "aws ec2 delete-route-table --route-table-id %s", rt_id);
            system(cmd);
        }
    }

    return 0;
}

int RDS::detach_and_destroy_gw()
{
    json_t *root;
    json_error_t error;
    char cmd[1024];
    char * json;

    sprintf(cmd, "aws ec2 describe-internet-gateways --filters Name=attachment.vpc-id,Values=%s", vpc_id_intern);
    if (execute_cmd(cmd, &json))
    {
        fprintf( stderr, "error: can not get internet gateways description\n");
        return -1;
    }

    root = json_loads( json, 0, &error );
    if ( !root )
    {
        fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
        return -1;
    }

    json_t * gws = json_object_get(root, "InternetGateways");
    if (gws == NULL)
    {
        fprintf( stderr, "error: can not parse internet gateways description\n");
        return -1;
    }
    size_t i;
    json_t * gw;
    const char * gw_id;
    json_array_foreach(gws, i, gw)
    {
        gw_id = json_string_value(json_object_get(gw, "InternetGatewayId"));
        sprintf(cmd, "aws ec2 detach-internet-gateway --internet-gateway-id=%s --vpc-id=%s", gw_id, vpc_id_intern);
        printf("%s\n", cmd);
        if (system(cmd) != 0)
        {
            fprintf( stderr, "error: can not detach gateway %s from vpc %s\n", gw_id, vpc_id_intern );
            return -1;
        }
        sprintf(cmd, "aws ec2 delete-internet-gateway --internet-gateway-id=%s", gw_id);
        printf("%s\n", cmd);
        if (system(cmd) != 0)
        {
            fprintf( stderr, "error: can not delete gateway %s\n", gw_id);
            return -1;
        }
    }
    return 0;
}

int RDS::create_vpc(const char **vpc_id)
{
    json_t *root;
    json_error_t error;
    char * result;
    char cmd[1024];

    if (execute_cmd((char *) "aws ec2 create-vpc --cidr-block 172.30.0.0/16", &result) != 0)
    {
        fprintf(stderr, "error: can not create VPC\n");
        return -1;
    }
    root = json_loads( result, 0, &error );
    if ( !root )
    {
        fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
        return -1;
    }
    *vpc_id = json_string_value(json_object_get(json_object_get(root, "Vpc"), "VpcId"));
    if (*vpc_id == NULL)
    {
        fprintf(stderr, "error: can not parse output of create-vpc command\n");
        return -1;
    }
    vpc_id_intern = * vpc_id;

    sprintf(cmd, "aws ec2 modify-vpc-attribute --enable-dns-support --vpc-id %s", *vpc_id);
    if (system(cmd) != 0)
    {
        fprintf(stderr, "error: can not enable dns support\n");
        return -1;
    }
    sprintf(cmd, "aws ec2 modify-vpc-attribute --enable-dns-hostnames --vpc-id %s", *vpc_id);
    if (system(cmd) != 0)
    {
        fprintf(stderr, "error: can not enable dns hostnames\n");
        return -1;
    }

    return 0;
}

int RDS::create_subnet(const char * az, const char * cidr, const char **subnet_id)
{
    json_t *root;
    json_error_t error;
    char * result;
    char cmd[1024];

    *subnet_id = NULL;
    sprintf(cmd, "aws ec2 create-subnet --cidr-block %s --availability-zone %s --vpc-id %s", cidr, az,
            vpc_id_intern);
    puts(cmd);
    if (execute_cmd(cmd, &result) != 0)
    {
        fprintf(stderr, "error: can not create subnet\n");
        return -1;
    }
    root = json_loads( result, 0, &error );
    if ( !root )
    {
        fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
        return -1;
    }
    *subnet_id = json_string_value(json_object_get(json_object_get(root, "Subnet"), "SubnetId"));
    if (*subnet_id == NULL)
    {
        fprintf(stderr, "error: can not parse output of create-vpc command\n");
        return -1;
    }

    if (subnets_intern == NULL)
    {
        subnets_intern = json_array();
    }
    json_array_append(subnets_intern, json_string(*subnet_id));

    sprintf(cmd, "aws ec2 modify-subnet-attribute --map-public-ip-on-launch --subnet-id %s", *subnet_id);
    if (system(cmd) != 0)
    {
        fprintf(stderr, "error: can not modify subnet attribute\n");
        return -1;
    }

    return 0;
}

int RDS::create_subnet_group()
{
    char cmd[1024];
    size_t i;
    json_t * subnet;

    sprintf(cmd,
            "aws rds create-db-subnet-group --db-subnet-group-name %s --db-subnet-group-description maxscale --subnet-ids",
            cluster_name_intern);
    json_array_foreach(subnets_intern, i, subnet)
    {
        strcat(cmd, " ");
        strcat(cmd, json_string_value(subnet));
    }
    subnets_group_name_intern = cluster_name_intern;
    if (system(cmd) != 0)
    {
        fprintf(stderr, "error: can not create subnets group\n");
        return -1;
    }

    return 0;
}

int RDS::create_gw(const char **gw_id)
{
    char * result;
    char cmd[1024];
    json_error_t error;

    *gw_id = NULL;
    gw_intern = NULL;
    if (execute_cmd((char *) "aws ec2 create-internet-gateway", &result) != 0)
    {
        fprintf(stderr, "error: can not create internet gateway\n");
        return -1;
    }
    json_t * root = json_loads( result, 0, &error );
    if ( !root )
    {
        fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
        return -1;
    }

    *gw_id = json_string_value(json_object_get(json_object_get(root, "InternetGateway"), "InternetGatewayId"));
    if (*gw_id == NULL)
    {
        fprintf(stderr, "error: can not parse output of create-internet-gateway command\n");
        return -1;
    }
    gw_intern = *gw_id;

    sprintf(cmd, "aws ec2 attach-internet-gateway --internet-gateway-id %s --vpc-id %s", *gw_id, vpc_id_intern);
    if (system(cmd) != 0)
    {
        fprintf(stderr, "error: can not attach gateway to VPC\n");
        return -1;
    }

    return 0;
}

int RDS::configure_route_table(const char **rt)
{
    char * result;
    char cmd[1024];
    json_error_t error;

    *rt = NULL;
    if (execute_cmd((char *) "aws ec2 describe-route-tables", &result) != 0)
    {
        fprintf(stderr, "error: can not get route tables description\n");
        return -1;
    }
    json_t * root = json_loads( result, 0, &error );
    if ( !root )
    {
        fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
        return -1;
    }

    json_t * route_tables = json_object_get(root, "RouteTables");
    if (route_tables == NULL)
    {
        fprintf( stderr, "error: can not parse route tables description\n");
        return -1;
    }
    size_t i;
    json_t * rtb;
    const char * rt_vpc;

    json_array_foreach(route_tables, i, rtb)
    {
        rt_vpc = json_string_value(json_object_get(rtb, "VpcId"));
        if (strcmp(vpc_id_intern, rt_vpc) == 0)
        {
            // add route to route table which belongs to give VPC
            *rt = json_string_value(json_object_get(rtb, "RouteTableId"));
            sprintf(cmd, "aws ec2 create-route --route-table-id %s --gateway-id %s --destination-cidr-block 0.0.0.0/0",
                    *rt, gw_intern);
            if (system(cmd) != 0)
            {
                fprintf( stderr, "error: can not create route\n");
                return -1;
            }
        }
    }
    if (*rt == NULL)
    {
        fprintf( stderr, "error: can not find route table\n");
        return -1;
    }
    return 0;
}

int RDS::create_cluster()
{
    char cmd[1024];
    char * result;
    json_error_t error;
    size_t i;

    sprintf(cmd,
            "aws rds create-db-cluster --database-name=test --engine=aurora --master-username=skysql --master-user-password=skysqlrds --db-cluster-identifier=%s --db-subnet-group-name=%s",
            cluster_name_intern, cluster_name_intern);

    execute_cmd(cmd , &result);
    json_t * root = json_loads( result, 0, &error );
    if ( !root )
    {
        fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
        return -1;
    }
    json_t * cluster = json_object_get(root, "DBCluster");
    cluster_intern = cluster;
    json_t * security_groups = json_object_get(cluster, "VpcSecurityGroups");
    json_t * sg;
    const char * sg_id;

    json_array_foreach(security_groups, i, sg)
    {
        sg_id = json_string_value(json_object_get(sg, "VpcSecurityGroupId"));
        printf("Security group %s\n", sg_id);
        sprintf(cmd,
                "aws ec2 authorize-security-group-ingress --group-id %s --protocol tcp --port 3306 --cidr 0.0.0.0/0", sg_id);
        system(cmd);
    }
    sg_intern = sg_id;

    for (size_t i = 0; i < N_intern; i++)
    {
        sprintf(cmd,
                "aws rds create-db-instance --db-cluster-identifier=%s --engine=aurora --db-instance-class=db.t2.medium --publicly-accessible --db-instance-identifier=node%03lu",
                cluster_name_intern, i);
        printf("%s\n", cmd);
        system(cmd);
    }
    return 0;
}

int RDS::get_writer(const char ** writer_name)
{
    char * json;
    char cmd[1024];
    sprintf(cmd, "aws rds describe-db-clusters --db-cluster-identifier=%s", cluster_name_intern);
    execute_cmd(cmd , &json);
    json_t * cluster = get_cluster_descr(json);
    json_t * nodes = json_object_get(cluster, "DBClusterMembers");

    //char * s = json_dumps(nodes, JSON_INDENT(4));
    //puts(s);

    bool writer;
    json_t * node;
    size_t i = 0;

    do
    {
        node = json_array_get(nodes, i);
        writer = json_is_true(json_object_get(node, "IsClusterWriter"));
        i++;
    }
    while (!writer);
    * writer_name = json_string_value(json_object_get(node, "DBInstanceIdentifier"));

    return 0;
}

int RDS::destroy_vpc()
{
    char cmd[1024];
    sprintf(cmd, "aws ec2 delete-vpc --vpc-id=%s", vpc_id_intern);
    return system(cmd);
}

int RDS::destroy_cluster()
{
    char cmd[1024];
    char * result;
    sprintf(cmd, "aws rds delete-db-cluster --db-cluster-identifier=%s --skip-final-snapshot",
            cluster_name_intern);
    return execute_cmd(cmd, &result);
}

int RDS::destroy_subnets_group()
{
    char cmd[1024];
    char * result;
    sprintf(cmd, "aws rds delete-db-subnet-group --db-subnet-group-name %s", get_subnetgroup_name());
    puts(cmd);
    return execute_cmd(cmd, &result);
}

int RDS::create_rds_db(int N)
{
    const char * vpc;
    const char * subnet1;
    const char * subnet2;
    const char * gw;
    const char * rt;

    N_intern = N;

    printf("Create VPC\n");
    if (create_vpc(&vpc) != 0)
    {
        fprintf( stderr, "error: can not create VPC\n");
        destroy_vpc();
        return -1;
    }
    printf("vpc id: %s\n", vpc);

    printf("Create subnets\n");
    create_subnet("eu-west-1b", "172.30.0.0/24", &subnet1);
    create_subnet("eu-west-1a", "172.30.1.0/24", &subnet2);

    printf("Create subnets group\n");
    if (create_subnet_group() != 0)
    {
        destroy_subnets();
        destroy_subnets_group();
        destroy_vpc();
        return -1;
    }

    printf("Create internet gateway\n");
    if (create_gw(&gw) != 0)
    {
        detach_and_destroy_gw();
        destroy_subnets();
        destroy_subnets_group();
        destroy_vpc();
        return -1;
    }
    printf("Gateway: %s\n", gw);

    printf("Configure route table\n");
    if (configure_route_table(&rt) != 0)
    {
        detach_and_destroy_gw();
        destroy_subnets();
        destroy_subnets_group();
        destroy_vpc();
        return -1;
    }
    printf("Route table: %s\n", rt);

    printf("Create RDS cluster\n");
    if (create_cluster() != 0)
    {
        destroy_nodes(get_cluster_nodes());
        destroy_cluster();
        detach_and_destroy_gw();
        destroy_subnets();
        destroy_subnets_group();
        destroy_vpc();
        return -1;
    }
    return 0;
}

int RDS::delete_rds_cluster()
{
    char * result;
    char cmd[1024];
    json_t * current_cluster;

    printf("Get cluster\n");
    cluster_intern = get_cluster();
    printf("Get cluster NODES\n");
    json_t * nodes = get_cluster_nodes();

    printf("Get subnets group: %s\n", get_subnetgroup_name());

    printf("Get subnets\n");
    get_subnets();

    printf("Get VPC: %s\n", vpc_id_intern);

    size_t alive_nodes = json_array_size(nodes);

    printf("Destroy nodes\n");
    destroy_nodes(nodes);

    do
    {
        printf("Waiting for nodes to be deleted, now %lu nodes are still alive\n", alive_nodes);
        sleep(5);
        current_cluster = get_cluster();
        nodes = get_cluster_nodes(current_cluster);
        alive_nodes = json_array_size(nodes);
    }
    while ( alive_nodes > 0);

    printf("Destroy cluster\n");
    destroy_cluster();

    do
    {
        printf("Waiting for cluster to be deleted\n");
        sleep(5);
        sprintf(cmd, "aws rds describe-db-clusters --db-cluster-identifier=%s", cluster_name_intern);
        execute_cmd(cmd, &result);

    }
    while (get_cluster_descr(result) != NULL);

    printf("Destroy subnets\n");
    destroy_subnets();

    printf("Destroy subnet group\n");
    destroy_subnets_group();

    printf("Get and destroy Internet Gateways\n");
    detach_and_destroy_gw();

    printf("Destroy vpc\n");
    return destroy_vpc();
}

int RDS::wait_for_nodes(size_t N)
{
    char * result;
    size_t active_nodes = 0;
    size_t i = 0;
    json_t * node;
    char cmd[1024];
    json_t * nodes;
    json_t * instances;
    json_t * instance;
    json_error_t error;

    do
    {
        printf("Waiting for nodes to be active, now %lu are active\n", active_nodes);
        sleep(5);
        cluster_intern = get_cluster();
        nodes = get_cluster_nodes();

        active_nodes = 0;
        json_array_foreach(nodes, i, node)
        {
            sprintf(cmd, "aws rds describe-db-instances --db-instance-identifier=%s", json_string_value(node));
            execute_cmd(cmd, &result);
            instances = json_loads( result, 0, &error );
            if ( !instances )
            {
                fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
                return -1;
            }
            instance = json_array_get(json_object_get(instances, "DBInstances"), 0);
            //puts(json_dumps(instance, JSON_INDENT(4)));
            if (strcmp(json_string_value(json_object_get(instance, "DBInstanceStatus")), "available") == 0)
            {
                active_nodes++;
            }
        }
    }
    while ( active_nodes != N);
    return 0;
}

int RDS::do_failover()
{
    char * result;
    const char * writer;
    const char * new_writer;
    char cmd[1024];
    if (get_writer(&writer) != 0)
    {
        return -1;
    }

    sprintf(cmd, "aws rds failover-db-cluster --db-cluster-identifier=%s", cluster_name_intern);
    if (execute_cmd(cmd, &result) != 0)
    {
        return -1;
    }
    do
    {
        if (get_writer(&new_writer) != 0)
        {
            return -1;
        }
        printf("writer: %s\n", new_writer);
        sleep(5);
    }
    while (strcmp(writer, new_writer) == 0);
    return 0;
}

json_t * RDS::get_endpoints()
{
    char cmd[1024];
    char * result;

    json_t *root;
    json_error_t error;

    json_t * node;
    json_t * node_json;
    json_t *endpoint;

    json_t * endpoints;

    endpoints = json_array();

    cluster_intern = get_cluster();
    json_t * nodes = get_cluster_nodes();
    //puts(json_dumps(nodes, JSON_INDENT(4)));

    size_t i;
    json_array_foreach(nodes, i, node)
    {
        sprintf(cmd, "aws rds describe-db-instances --db-instance-identifier=%s", json_string_value(node));
        if (execute_cmd(cmd, &result) != 0)
        {
            fprintf( stderr, "error: executing aws rds describe-db-instances\n");
            return NULL;
        }
        root = json_loads( result, 0, &error );
        if ( !root )
        {
            fprintf( stderr, "error: on line %d: %s\n", error.line, error.text );
            return NULL;
        }
        node_json = json_array_get(json_object_get(root, "DBInstances"), 0);
        endpoint = json_object_get(node_json, "Endpoint");
        json_array_append(endpoints, endpoint);
    }
    return endpoints;
}
