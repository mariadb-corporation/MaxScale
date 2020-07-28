/**
 * @file create_rds.cpp Creates RDS Aurora cluster with 4 instances
 * Creates VPC, subnets, subnets group, internet gateway, routing table, routes, configure security group
 * In case of any error tries to remove all created stuff
 */

#include <iostream>
#include <unistd.h>
#include "testconnections.h"
#include <jansson.h>
#include "rds_vpc.h"

int main(int argc, char* argv[])
{
    RDS* cluster = new RDS((char*) "auroratest");
    return cluster->create_rds_db(4);
}
