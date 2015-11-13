#RabbitMQ Consumer Client

## Overview

This utility tool is used to read messages from a RabbitMQ broker sent by the [RabbitMQ Filter](RabbitMQ-Filter.md) and forward these messages into an SQL database as queries.

## Command Line Arguments

The **RabbitMQ Consumer Client** only has one command line argument.

| Command | Argument                                        |
|---------|-------------------------------------------------|
| -c | Path to the folder containing the configuration file |

## Installation

To install the RabbitMQ Consumer Client you ca either use the provided packages or you can compile it from source code. The source code is included as a part of the MaxScale source code and can be found in the `rabbitmq_consumer` folder. Please refer to the [README](../../rabbitmq_consumer/README) in the folder for more detailed instructions about installation and configuration.

## Configuration

The consumer client requires that the `consumer.cnf` configuration file is either be present in the `etc` folder of the installation directory or in the folder specified by the `-c` argument.

The source broker, the destination database and the message log file can be configured into the separate `consumer.cnf` file.

| Option     | Description                                 |
|-----------|---------------------------------------------|
| hostname	| Hostname of the RabbitMQ server              |
| port		| Port of the RabbitMQ server                  |
| vhost		| Virtual host location of the RabbitMQ server |
| user		| Username for the RabbitMQ server             |
| passwd	| Password for the RabbitMQ server             |
| queue		| Queue to consume from                        |
| dbserver	| Hostname of the SQL server                   | 
| dbport	| Port of the SQL server                       |
| dbname	| Name of the SQL database to use              |
| dbuser	| Database username                            |
| dbpasswd	| Database password                            |
| logfile	| Message log filename                         |
