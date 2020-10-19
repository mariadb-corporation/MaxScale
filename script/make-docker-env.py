#!/usr/bin/env python3
#
# Copyright (c) 2019 MariaDB Corporation Ab
#
#  Use of this software is governed by the Business Source License included
#  in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
#  Change Date: 2024-10-14
#
#  On the date above, in accordance with the Business Source License, use
#  of this software will be governed by version 2 or later of the General
#  Public License.
#

import sys
import os
import configparser
from io import StringIO
from argparse import ArgumentParser
from enum import Enum
import shutil

# Global ArgumentParser parsed command line
cmd_line_args = None

# What types of servers a host has
class HostType(Enum):
    Master = 1
    Slave  = 2
    Both   = 3

# Types of files to generate
class File(Enum):
    DockerCompose = 1
    MaxScaleCnf   = 2
    SystemTestCnf = 3
    MasterSql     = 4
    SlaveSql      = 5

# Constants. Currently all maxscale.cnf user and password entries are changed to
# default_user and default_passwd in order to make life simpler (TODO?).
default_user="maxskysql"
default_passwd="skysql"
db_image="mariadb:10.4"
root_passwd="mariadb"

help_text = '''
This script reads a maxscale.cnf file and creates a docker-compose setup
matching the maxscale configuration. The docker configurations are ready
to be copied and started on their respective hosts. See docker-remote.py.
A cleaned up maxscale.cnf file, and a maxscale.cnf-systest file are also
generated.
'''

#### function main
def main():
    parser = ArgumentParser(help_text)
    parser.add_argument("maxscale_cnf", nargs='?',
                        help="The maxscale config file to read. Default ./maxscale.cnf")

    parser.add_argument("-o", "--outdir", type=str, default="docker-env",
                        help="output directory. Default docker-env")

    parser.add_argument("-d", "--delete_outdir", action="store_true", default=False,
                        help="Delete outdir before generation if it exists")

    parser.add_argument("-e", "--examples", action="store_true",
                        help="print some helpful examples")

    global cmd_line_args
    cmd_line_args = parser.parse_args()

    if cmd_line_args.examples:
        print_examples()
        sys.exit(0)

    if cmd_line_args.maxscale_cnf == None:
        cmd_line_args.maxscale_cnf = "maxscale.cnf"

    if os.path.exists(cmd_line_args.outdir):
        if not cmd_line_args.delete_outdir:
            print('Output directory "%s" exists.\nDelete and continue (yN)?' % cmd_line_args.outdir)

            a = input().lower()
            if (len(a)==0 or(len(a)!=0 and a!='y')):
                print("Exit without generating files")
                sys.exit(0)

        shutil.rmtree(cmd_line_args.outdir)

    # Parse maxscale.cnf into a Config instance
    config = Config(cmd_line_args.maxscale_cnf)

    # Create dictionaries of key-value pairs that are needed for the generated
    # files, e.g. {"password", "skysql"}
    maxscale_params, systest_params = create_parameter_dicts(config)

    # Generate modified maxscale.cnf and maxscale.cnf.systest
    print(config.template.format(**maxscale_params), file=open_file(File.MaxScaleCnf, None))
    print(config.template.format(**systest_params), file=open_file(File.SystemTestCnf, None))

    # Genrate the rest of the files
    generate_files(config, maxscale_params, systest_params)


#### class Config reads the maxscale.cnf file.
# It creates the Server objects, and a string representation of maxscale.cnf
# with items that will change, replaced with formatting place holders
# e.g. "threads = auto" => "threads = {threads}".
class Config(object):
    def __init__(self, maxscale_cnf):
        cnf = configparser.ConfigParser()
        cnf.readfp(open(maxscale_cnf))

        self.servers = self.read_servers(cnf)
        self.threads = cnf.get("maxscale", "threads", fallback="1")
        self.replace_server_sections(cnf, self.servers)
        self.insert_placefolders(cnf)

        string_io = StringIO()
        cnf.write(string_io)
        self.template = string_io.getvalue()

    @staticmethod
    def read_servers(cnf):
        servers = []
        server_index=0
        for sect in cnf.sections():
            if  cnf.has_option(sect, "type") and cnf.get(sect, "type") == "server":
                servers.append(Server(server_index, sect, cnf.items(sect)))
                server_index += 1
        return servers

    @staticmethod
    def insert_placefolders(cnf): # other than those in server sections
        cnf.set("maxscale", "threads", "{threads}")
        for sect in cnf.sections():
            if  cnf.has_option(sect, "user"):
                cnf.set(sect, "user", "{user}")
                cnf.set(sect, "password", "{password}")

            if  cnf.has_option(sect, "replication_user"):
                cnf.set(sect, "replication_user", "{user}")
                cnf.set(sect, "replication_password", "{password}")

            if  cnf.has_option(sect, "servers"):
                cnf.set(sect, "servers", "{server_list}");

    @staticmethod
    def replace_server_sections(cnf, servers):
        for server in servers:
            cnf.remove_section(server.name)
            server_placeholder = "{server%d}" % server.server_index
            cnf.add_section(server_placeholder)
            for key, value in server.config_items.items():
                cnf.set(server_placeholder, key, value)


#### class Server
# In-memory representation of a Server, read from a maxscale.cnf file.
# The parameters and member config_items contains all items from the maxscale.cnf
# file for this server, the rest of the members are for replacing specific items.
class Server(object):
    def __init__(self, server_index, name, config_items):
        self.is_master = server_index==0
        self.server_index = server_index
        self.name = name
        self.config_items = dict(config_items)

        self.address=self.config_items["address"]
        self.port = int(self.config_items["port"]) if "port" in self.config_items else 3306
        self.name = name

        self.test_address="###node_server_IP_%d###" % (server_index+1)
        self.test_port = "###node_port_%d###" % (server_index+1)
        self.test_name = "server%d" % (server_index+1)


#### function generate_files generates the docker yaml and sql files.
def generate_files(config, maxscale_params, systest_params):

    servers = sorted(config.servers, key=lambda server: server.address)

    current_ip = servers[0].address
    host_type = None

    docker_file = start_docker_file(current_ip, maxscale_params)

    for server in servers:
        if current_ip!=server.address:
            finish_docker_file(current_ip, maxscale_params, host_type)
            host_type = None
            current_ip = server.address
            docker_file = start_docker_file(current_ip, maxscale_params)

        volume=""
        if server.is_master:
            volume = "./sql/master:/docker-entrypoint-initdb.d"
            host_type = HostType.Master if host_type==None or host_type==HostType.Master else HostType.Both
        else:
            volume = "./sql/slave:/docker-entrypoint-initdb.d"
            host_type = HostType.Slave if host_type==None or host_type==HostType.Slave else HostType.Both

        print('''    %s:
        image: %s
        network_mode: "host"
        environment:
            MYSQL_ROOT_PASSWORD: %s
        volumes:
            - %s
        command: mysqld --log-bin=binlog --binlog-format=ROW --server-id=%d --port=%d --log-slave-updates\n'''
        % (server.name, db_image, root_passwd, volume, 1000+server.server_index, server.port),
    file=docker_file)

    finish_docker_file(current_ip, maxscale_params, host_type)


#### function start_docker_file opens a new docker-file, and writes the header into it.
#### The function finish_docker_file is called once the docker file has been created
#### to generate the master and slave sql files.
def start_docker_file(ip, maxscale_params):
    docker_file = open_file(File.DockerCompose, ip)
    print('''version: "3.3"\nservices:''', file=docker_file);

    return docker_file


#### function finish_docker_file writes the sql files.
# The reason there is a "finish" function is: while the
# yaml file is iterating over servers for a specific IP, it
# also notes if the servers are master, slave or both, which
# is used here to avoid writing sql files that would not be used.
def finish_docker_file(ip, maxscale_params, host_type):
    if host_type==HostType.Master or host_type==HostType.Both:
        create_master_sql(maxscale_params, ip)

    if host_type==HostType.Slave or host_type==HostType.Both:
        create_slave_sql(maxscale_params, ip)


#### function open_file, opens the files to be generated for writing
# Creates the requested file, error if it already exists.
def open_file(file_enum, ip):
    directory = cmd_line_args.outdir
    if ip!=None: directory = directory + '/' + ip

    if file_enum == File.DockerCompose:
        file_name = 'docker-compose.yml'

    elif file_enum == File.MaxScaleCnf:
        file_name = 'maxscale.cnf'

    elif file_enum == File.SystemTestCnf:
        file_name = 'maxscale.cnf-systest'

    elif file_enum == File.MasterSql:
        directory = directory + "/sql/master"
        file_name = '01-create-users.sql'

    elif file_enum == File.SlaveSql:
        directory = directory + "/sql/slave"
        file_name = "01-setup-replication.sql"


    if not os.path.exists(directory):
        os.makedirs(directory)
        os.chmod(directory, 0o777)

    file_name = directory + '/' + file_name

    if os.path.isfile(file_name):
        raise RuntimeError('File "%s" already exists', file_name)

    ret = open(file_name, "w")
    os.chmod(file_name, 0o666)

    return ret


#### function create_parameter_dicts,
# Creates parameter dictionaries. key-value pairs, for file genration.
def create_parameter_dicts(config):
    maxscale_params={"threads" : config.threads, "user" : default_user, "password" : default_passwd}
    systest_params={"threads" : "###threads###", "user" : default_user, "password" : default_passwd}
    local_servers_list=""
    tester_servers_list=""
    for server in config.servers:
        if len(local_servers_list): local_servers_list += ', '
        local_servers_list += server.name

        if len(tester_servers_list): tester_servers_list += ', '
        tester_servers_list += server.test_name

        maxscale_params.update({"server%d" % server.server_index : server.name})
        systest_params.update({"server%d" % server.server_index : server.test_name})

        maxscale_params.update({"address%d" % server.server_index : server.address})
        systest_params.update({"address%d" % server.server_index : server.test_address})

        maxscale_params.update({"port%d" % server.server_index : server.port})
        systest_params.update({"port%d" % server.server_index : server.test_port})

    maxscale_params.update({"server_list" : local_servers_list})
    systest_params.update({"server_list" : tester_servers_list})

    return (maxscale_params, systest_params)


#### function create_master_sql,
def create_master_sql(params, ip):
    create_users_sql='''
reset master;
create database dummy_for_gtid; -- gtid becomes 0-1000-1
create user '{user}'@'{address0}' identified by '{password}';
create user '{user}'@'localhost' identified by '{password}';
create user '{user}'@'%' identified by '{password}';
grant all on *.* to '{user}'@'{address0}' with grant option;
grant all on *.* to '{user}'@'localhost' with grant option;
grant all on *.* to '{user}'@'%' with grant option;
flush privileges;
create database test;
    '''.format(**params)

    master_file = open_file(File.MasterSql, ip)
    print(create_users_sql, file=master_file)


#### function create_slave_sql,
def create_slave_sql(params, ip):
    slave_replication_sql='''
SET GLOBAL gtid_slave_pos = "0-1000-1";
change master to master_host='{address0}',
master_port={port0},
master_user='{user}',
master_password='{password}',
master_use_gtid=slave_pos,
master_connect_retry=1;
start slave;
    '''.format(**params)

    slave_file = open_file(File.SlaveSql, ip)
    print(slave_replication_sql, file=slave_file)


def print_examples():
    print("Examples:")
    print("System up. down, status. The containers are deleted on 'down'.")
    print("  docker-compose up -d")
    print("  docker-compose down")
    print("  docker-compose ps")
    print("")
    print("Keep the containers and data around:")
    print("  docker-compose create")
    print("  docker-compose start")
    print("  docker-compose stop")
    print("  docker-compose rm  # to remove containers")
    print("")
    print("Other useful docker commands:")
    print("  docker container ls --all  # all existing containers")
    print("  docker exec -i -t <container> <command>  # e.g. docker exec -i -t master /bin/bash")
    print("  docker cp maxscale.cnf.local to <your-etc-path>.")
    print("  docker cp maxscale.cnf.systest to maxscale-system-test/cnf if needed.")
    print("")
    print("Running local maxscale:")
    print("  ./maxscale -d --configdir=<your-etc-path>")

if __name__ == "__main__":
    main()
