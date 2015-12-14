# Maxadmin

# The Maxscale Administrative

# & Monitoring Client Application

Mark Riddoch

Last Updated: 24th June 2015

 - [Overview](#overview)
 - [Running MaxAdmin](#running)
 - [Working With Administration Interface Users](#interface)
 - [Getting Help](#help)
 - [Working with Services](#services)
 - [Working with Servers](#servers)
 - [Working with Sessions](#sessions)
 - [Descriptor Control Blocks](#dcbs)
 - [Working with Filters](#filters)
 - [Working with Monitors](#monitors)
 - [MaxScale Status Commands](#statuscommands)
 - [Administration Commands](#admincommands)
 - [Configuring MaxScale to Accept MaxAdmin Connections](#connections)
 - [Tuning MaxScale](#tuning)

<a name="overview"></a>
# Overview

MaxAdmin is a simple client interface that can be used to interact with the MaxScale server, it allows the display of internal MaxScale statistics, status and control of MaxScale operations.

MaxAdmin supports

* Interactive user sessions

* Execution of one-off commands via command line arguments

* Execution of command scripts

<a name="running"></a>
# Running MaxAdmin

The MaxAdmin client application may be run in two different modes, either as an interactive command shell for executing commands against MaxScale or by passing commands on the MaxAdmin command line itself.

<a name="interface"></a>
# Working With Administration Interface Users

A default installation of MaxScale allows connection to the administration interface using the username of `admin` and the password `mariadb`. This username and password stay in effect as long as no other users have been created for the administration interface. As soon as the first user is added the use of `admin/mariadb` as login credentials will be disabled.


## What Users Have Been Defined?

In order to see the current users that have been defined for the administration interface use the command show users.

    MaxScale> show users
    Administration interface users:
    Users table data
    Hashtable: 0x734470, size 52
    	No. of entries:     		5
    	Average chain length:	0.1
    	Longest chain length:	2
    User names: vilho, root, dba, massi, mark
    MaxScale>

Please note that if no users have been configured the default admin/mariadb user will not be shown.

    MaxScale> show users
    Administration interface users:
    No administration users have been defined.
    MaxScale> 

## Add A New User

To add a new administrative user to the MaxScale server use the command add user. This command is passed a user name and a password.

    MaxScale> add user maria dtbse243
    User maria has been successfully added.
    MaxScale> 

## Delete A User

To remove a user the command remove user is used, it must also be called with the username and password of the user. The password will be checked.

    MaxScale> remove user maria des
    Failed to remove user maria. Authentication failed
    MaxScale> remove user maria dtbse243
    User maria has been successfully removed.
    MaxScale> 

# Command Line Switches

The MaxAdmin command accepts a number of switches

<table>
  <tr>
    <td>Switch</td>
    <td>Long Option</td>
    <td>Description</td>
  </tr>
  <tr>
    <td>-u user</td>
    <td>--user=...</td>
    <td>Sets the username that will be used for the MaxScale connection. If no -u option is passed on the MaxAdmin command line then the default username of ‘admin’ will be used.</td>
  </tr>
  <tr>
    <td>-p password</td>
    <td>--password=...</td>
    <td>Sets the user password that will be used. If no -p option is passed on the command line then MaxAdmin will prompt for interactive entry of the password.</td>
  </tr>
  <tr>
    <td>-h hostname</td>
    <td>--hostname=...</td>
    <td>The hostname of the MaxScale server to connect to. If no -h option is passed on the command line then MaxAdmin will attempt to connect to the host ‘localhost’.</td>
  </tr>
  <tr>
    <td>-P port</td>
    <td>--port=...</td>
    <td>The port that MaxAdmin will use to connect to the MaxScale server. if no -P option is given then the default port of 6603 will be used.</td>
  </tr>
  <tr>
    <td>-?</td>
    <td>--help</td>
    <td>Print usage information regarding MaxAdmin</td>
  </tr>
  <tr>
    <td>-v</td>
    <td>--version</td>
    <td>Print the maxadmin version information and exit</td>
  </tr>
</table>


When a switch takes a value, this may either be as the next argument on the command line or maybe as part of the switch itself. E.g. -u me and -ume are treated in the same way.

## Interactive Operation

If no arguments other than the command line switches are passed to MaxAdmin it will enter its interactive mode of operation. Users will be prompted to enter commands with a **MaxScale>** prompt. The commands themselves are documented in the sections later in this document. A help system is available that will give some minimal details of the commands available.

Command history is available on platforms that support the libedit library. This allows the use of the up and down arrow keys to recall previous commands that have been executed by MaxAdmin. The default edit mode for the history is to emulate the vi commands, the behavior of libedit may however be customized using the .editrc file. To obtain the history of commands that have been executed use the inbuilt history command.

In interactive mode it is possible to execute a set of commands stored in an external file by using the source command. The command takes the argument of a filename which should contain a set of MaxScale commands, one per line. These will be executed in the order they appear in the file.

## Command Line Operation

MaxAdmin can also be used to execute commands that are passed on the command line, e.g.
    
    -bash-4.1$ maxadmin -hmaxscale list services
    Password: 
    Services.
    --------------------------+----------------------+--------+---------------
    Service Name              | Router Module        | #Users | Total Sessions
    --------------------------+----------------------+--------+---------------
    Test Service              | readconnroute        |      1 |     1
    Split Service             | readwritesplit       |      1 |     1
    Filter Service            | readconnroute        |      1 |     1
    QLA Service               | readconnroute        |      1 |     1
    Debug Service             | debugcli             |      1 |     1
    CLI                       | cli                  |      2 |    27
    --------------------------+----------------------+--------+---------------
    -bash-4.1$ 

The single command is executed and MaxAdmin then terminates. If the -p option is not given then MaxAdmin will prompt for a password. If a MaxScale command requires an argument which contains whitespace, for example a service name, that name should be quoted. The quotes will be preserved and used in the execution of the MaxScale command.
    
    -bash-4.1$ maxadmin show service "QLA Service"
        Password: 
        Service 0x70c6a0
        	Service:		QLA Service
        	Router:			readconnroute (0x7ffff0f7ae60)
        	Number of router sessions:   	0
        	Current no. of router sessions:	0
        	Number of queries forwarded:   	0
        	Started:		Wed Jun 25 10:08:23 2014
        	Backend databases
        		127.0.0.1:3309  Protocol: MySQLBackend
        		127.0.0.1:3308  Protocol: MySQLBackend
        		127.0.0.1:3307  Protocol: MySQLBackend
        		127.0.0.1:3306  Protocol: MySQLBackend
        	Users data:        	0x724340
        	Total connections:	1
        	Currently connected:	1
        -bash-4.1$ 

Command files may be executed by either calling MaxAdmin with the name of the file that contains the commands

    maxadmin listall.ms

Or by using the #! mechanism to make the command file executable from the shell. To do this add a line at the start of your command file that contains the #! directive with the path of the MaxAdmin executable. Command options may also be given in this line. For example to create a script file that runs a set of list commands
    
    #!/usr/local/bin/maxadmin -hmaxscalehost
    list modules
    list servers
    list services
    list listeners
    list dcbs
    list sessions
    list filters

Then simply set this file to have execute permissions and it may be run like any other command in the Linux shell.

## The .maxadmin file

MaxAdmin supports a mechanism to set defaults for all the command line switches via a file in the home directory of the user. If a file named .maxadmin exists it will be read and parameters set according to the lies in this files. The parameters that can be set are hostname, port, user and passwd. An example of a .maxadmin file that will alter the default password and user name arguments would be

    user=mark
    passwd=max5cal3

This mechanism can be used to provide a means of passwords entry into maxadmin or to override any of the command line option defaults. If a command line option is given that will still override the value in the .maxadmin file.

The .maxadmin file may  be made read only to protect any passwords written to that file.

<a name="help"></a> 
# Getting Help

A help system is available that describes the commands available via the administration interface. To obtain a list of all commands available simply type the command help.
    
    MaxScale> help
    Available commands:
        add user
        clear server
        disable [heartbeat|log|root]
        enable [heartbeat|log|root]
        list [clients|dcbs|filters|listeners|modules|monitors|services|servers|sessions]
        reload [config|dbusers]
        remove user
        restart [monitor|service]
        set server
        show [dcbs|dcb|dbusers|epoll|filter|filters|modules|monitor|monitors|server|servers|services|service|session|sessions|users]
        shutdown [maxscale|monitor|service]
    
    Type help command to see details of each command.
    Where commands require names as arguments and these names contain
    whitespace either the \ character may be used to escape the whitespace
    or the name may be enclosed in double quotes ".
    MaxScale> 

To see more detail on a particular command, and a list of the sub commands of the command, type help followed by the command name.

    MaxScale> help list
    Available options to the list command:
        clients    List all the client connections to MaxScale
        dcbs       List all the DCBs active within MaxScale
        filters    List all the filters defined within MaxScale
        listeners  List all the listeners defined within MaxScale
        modules    List all currently loaded modules
        monitors   List all monitors
        services   List all the services defined within MaxScale
        servers    List all the servers defined within MaxScale
        sessions   List all the active sessions within MaxScale
    MaxScale> 

<a name="services"></a> 
# Working With Services

A service is a very important concept in MaxScale as it defines the mechanism by which clients interact with MaxScale and can attached to the backend databases. A number of commands exist that allow interaction with the services.

## What Services Are Available?

The list services command can be used to discover what services are currently available within your MaxScale configuration.
    
    MaxScale> list services
    Services.
    --------------------------+----------------------+--------+---------------
    Service Name              | Router Module        | #Users | Total Sessions
    --------------------------+----------------------+--------+---------------
    Test Service              | readconnroute        |      1 |     1
    Split Service             | readwritesplit       |      1 |     1
    Filter Service            | readconnroute        |      1 |     1
    QLA Service               | readconnroute        |      1 |     1
    Debug Service             | debugcli             |      1 |     1
    CLI                       | cli                  |      2 |    24
    --------------------------+----------------------+--------+---------------
    MaxScale> 

In order to determine which ports services are using then the list listeners command can be used.

    MaxScale> list listeners
    Listeners.
    ---------------------+--------------------+-----------------+-------+--------
    Service Name         | Protocol Module    | Address         | Port  | State
    ---------------------+--------------------+-----------------+-------+--------
    Test Service         | MySQLClient        | *               |  4006 | Running
    Split Service        | MySQLClient        | *               |  4007 | Running
    Filter Service       | MySQLClient        | *               |  4008 | Running
    QLA Service          | MySQLClient        | *               |  4009 | Running
    Debug Service        | telnetd            | localhost       |  4242 | Running
    CLI                  | maxscaled          | localhost       |  6603 | Running
    ---------------------+--------------------+-----------------+-------+--------
    MaxScale> 

## See Service Details

It is possible to see the details of an individual service using the show service command. This command should be passed the name of the service you wish to examine as an argument. Where a service name contains spaces characters there should either be escaped or the name placed in quotes.

    MaxScale> show service "QLA Service"
    Service 0x70c6a0
    	Service:				QLA Service
    	Router:				readconnroute (0x7ffff0f7ae60)
    	Number of router sessions:   	0
    	Current no. of router sessions:	0
    	Number of queries forwarded:   	0
    	Started:				Wed Jun 25 10:08:23 2014
    	Backend databases
    		127.0.0.1:3309  Protocol: MySQLBackend
    		127.0.0.1:3308  Protocol: MySQLBackend
    		127.0.0.1:3307  Protocol: MySQLBackend
    		127.0.0.1:3306  Protocol: MySQLBackend
    	Users data:       		 	0x724340
    	Total connections:			1
    	Currently connected:			1
    MaxScale> 

This allows the set of backend servers defined by the service to be seen along with the service statistics and other information.

## Examining Service Users

MaxScale provides an authentication model by which the client application authenticates with MaxScale using the credentials they would normally use to with the database itself. MaxScale loads the user data from one of the backend databases defined for the service. The show dbusers command can be used to examine the user data held by MaxScale.

    MaxScale> show dbusers "Filter Service"
    Users table data
    Hashtable: 0x723e50, size 52
    	No. of entries:     	48
    	Average chain length:	0.9
    	Longest chain length:	5
    User names: pappo@%, rana@%, new_control@%, new_nuovo@%, uno@192.168.56.1, nuovo@192.168.56.1, pesce@%, tryme@192.168.1.199, repluser@%, seven@%, due@%, pippo@%, mmm@%, daka@127.0.0.1, timour@%, ivan@%, prova@%, changeme@127.0.0.1, uno@%, massimiliano@127.0.0.1, massim@127.0.0.1, massi@127.0.0.1, masssi@127.0.0.1, pappo@127.0.0.1, rana@127.0.0.1, newadded@127.0.0.1, newaded@127.0.0.1, pesce@127.0.0.1, repluser@127.0.0.1, seven@127.0.0.1, pippo@127.0.0.1, due@127.0.0.1, nopwd@127.0.0.1, timour@127.0.0.1, controlla@192.168.56.1, ivan@127.0.0.1, ppp@127.0.0.1, daka@%, nuovo@127.0.0.1, uno@127.0.0.1, repluser@192.168.56.1, havoc@%, tekka@192.168.1.19, due@192.168.56.1, qwerty@127.0.0.1, massimiliano@%, massi@%, massim@%
    MaxScale>

## Reloading Service User Data

MaxScale will automatically reload user data if there are failed authentication requests from client applications. This reloading is rate limited and triggered by missing entries in the MaxScale table. If a user is removed from the backend database user table it will not trigger removal from the MaxScale internal table. The reload dbusers command can be used to force the reloading of the user table within MaxScale.

    MaxScale> reload dbusers "Split Service"
    Loaded 34 database users for service Split Service.
    MaxScale> 

## Stopping A Service

It is possible to stop a service from accepting new connections by using the shutdown service command. This will not affect the connections that are already in place for a service, but will stop any new connections from being accepted.

    MaxScale> shutdown service "Split Service"
    MaxScale>

## Restart A Stopped Service

A stopped service may be restarted by using the restart service command.

    MaxScale> restart service "Split Service"
    MaxScale> 

<a name="servers"></a> 
# Working With Servers

The server represents each of the instances of MySQL or MariaDB that a service may use. 

## What Servers Are Configured?

The command list servers can be used to display a list of all the servers configured within MaxScale.

    MaxScale> list servers
    Servers.
    -------------------+-----------------+-------+----------------------+------------
    Server             | Address         | Port  | Status               | Connections
    -------------------+-----------------+-------+----------------------+------------
    server1            | 127.0.0.1       |  3306 | Running              |    0
    server2            | 127.0.0.1       |  3307 | Master, Running      |    0
    server3            | 127.0.0.1       |  3308 | Running              |    0
    server4            | 127.0.0.1       |  3309 | Slave, Running       |    0
    -------------------+-----------------+-------+----------------------+------------
    MaxScale>

## Server Details

It is possible to see more details regarding a given server using the show server command.

    MaxScale> show server server2
    Server 0x70d460 (server2)
    	Server:			127.0.0.1
    	Status:               	Master, Running
    	Protocol:			MySQLBackend
    	Port:				3307
    	Server Version:		5.5.25-MariaDB-log
    	Node Id:			124
    	Number of connections:          0
    	Current no. of conns:           0
        Current no. of operations:      0
    MaxScale> 

If the server has a non-zero value set for the server configuration item "persistpoolmax",
then additional information will be shown:

        Persistent pool size:            1
        Persistent measured pool size:   1
        Persistent pool max size:            10
        Persistent max time (secs):          3660

The distinction between pool size and measured pool size is that the first is a
counter that is updated when operations affect the persistent connections pool,
whereas the measured size is the result of checking how many persistent connections
are currently in the pool. It can be slightly different, since any expired
connections are removed during the check.

## Setting The State Of A Server

MaxScale maintains a number of status bits for each server that is configured, these status bits are normally maintained by the monitors, there are two commands in the user interface that are used to manually maintain these bits also; the set server and clear server commands.

The status bit that can be controlled are

<table>
  <tr>
    <td>Bit Name</td>
    <td>Description</td>
  </tr>
  <tr>
    <td>running</td>
    <td>The server is responding to requests, accepting connections and executing database commands</td>
  </tr>
  <tr>
    <td>master</td>
    <td>The server is a master in a replication setup or should be considered as a destination for database updates.</td>
  </tr>
  <tr>
    <td>slave</td>
    <td>The server is a replication slave or is considered as a read only database.</td>
  </tr>
  <tr>
    <td>synced</td>
    <td>The server is a fully fledged member of a Galera cluster</td>
  </tr>
  <tr>
    <td>maintenance</td>
    <td>The server is in maintenance mode. In this mode no new connections will be established to the server. The monitors will also not monitor servers that are in maintenance mode.</td>
  </tr>
</table>


All status bits, with the exception of the maintenance bit, will be set by the monitors that are monitoring the server. If manual control is required the monitor should be stopped.

    MaxScale> set server server3 maintenance
    MaxScale> clear server server3 maintenance
    MaxScale> 

## Viewing the persistent pool of DCB

The DCBs that are in the pool for a particular server can be displayed (in the
format described below in the DCB section) with a command like:

    MaxScale> show persistent server1

<a name="sessions"></a> 
# Working With Sessions

The MaxScale session represents the state within MaxScale. Sessions are dynamic entities and not named in the configuration file, this means that sessions can not be easily named within the user interface. The sessions are referenced using ID values, these are actually memory address, however the important thing is that no two session have the same ID.

## What Sessions Are Active in MaxScale?

There are a number of ways to find out what sessions are active, the most comprehensive being the list sessions command.

    MaxScale> list sessions
    Sessions.
    -----------------+-----------------+----------------+--------------------------
    Session          | Client          | Service        | State
    -----------------+-----------------+----------------+--------------------------
    0x7267a0         | 127.0.0.1       | CLI            | Session ready for routing
    0x726340         |                 | CLI            | Listener Session
    0x725720         |                 | Debug Service  | Listener Session
    0x724720         |                 | QLA Service    | Listener Session
    0x72a750         |                 | Filter Service | Listener Session
    0x709500         |                 | Split Service  | Listener Session
    0x7092d0         |                 | Test Service   | Listener Session
    -----------------+-----------------+----------------+--------------------------
    MaxScale>

This lists all the sessions for both user connections and for the service listeners.

The list clients command will give just the subset of sessions that originate from a client connection.

    MaxScale> list clients
    Client Connections
    -----------------+------------+----------------------+------------
     Client          | DCB        | Service              | Session
    -----------------+------------+----------------------+------------
     127.0.0.1       |   0x7274b0 | CLI                  |   0x727700
     127.0.0.1       |   0x727900 | QLA Service          |   0x727da0
    -----------------+------------+----------------------+------------
    MaxScale>

## Display Session Details

Once the session ID has been determined using one of the above method it is possible to determine more detail regarding a session by using the show session command.

    MaxScale> show session 0x727da0
    Session 0x727da0
    	State:    		Session ready for routing
    	Service:		QLA Service (0x70d6a0)
    	Client DCB:		0x727900
    	Client Address:	127.0.0.1
    	Connected:		Wed Jun 25 15:27:21 2014
    MaxScale> 

<a name="dcbs"></a> 
# Descriptor Control Blocks

The Descriptor Control Block or DCB is a very important entity within MaxScale, it represents the state of each connection within MaxScale. A DCB is allocated for every connection from a client, every network listener and every connection to a backend database. Statistics for each of these connections are maintained within these DCB’s.

As with session above the DCB’s are not named and are therefore referred to by the use of a unique ID, the memory address of the DCB.

## Finding DCB’s

There are several ways to determine what DCB’s are active within a MaxScale server, the most straightforward being the list dcbs command.

    MaxScale> list dcbs
    Descriptor Control Blocks
    ------------+----------------------------+----------------------+----------
     DCB        | State                      | Service              | Remote
    ------------+----------------------------+----------------------+----------
       0x667170 | DCB for listening socket   | Test Service         | 
       0x71a350 | DCB for listening socket   | Split Service        | 
       0x724b40 | DCB for listening socket   | Filter Service       | 
       0x7250d0 | DCB for listening socket   | QLA Service          | 
       0x725740 | DCB for listening socket   | Debug Service        | 
       0x726740 | DCB for listening socket   | CLI                  | 
       0x7274b0 | DCB in the polling loop    | CLI                  | 127.0.0.1
       0x727900 | DCB in the polling loop    | QLA Service          | 127.0.0.1
       0x72e880 | DCB in the polling loop    | QLA Service          | 
    ------------+----------------------------+----------------------+----------
    MaxScale> 

A MaxScale server that has activity on it will however have many more DCB’s than in the example above, making it hard to find the DCB that you require. The DCB ID is also included in a number of other command outputs, depending on the information you have it may be easier to use other methods to locate a particular DCB.

## DCB Of A Client Connection

To find the DCB for a particular client connection it may be best to start with the list clients command and then look at each DCB for a particular client address to determine the one of interest.

## DCB Details

The details of an individual DCB can be obtained by use of the show dcb command

    MaxScale> show dcb 0x727900
    DCB: 0x727900
    	DCB state: 		DCB in the polling loop
        Username:               somename
        Protocol:               MySQLBackend
        Server Status:          Master, running
        Role:                   Request Handler
    	Connected to:		127.0.0.1
    	Owning Session:   	0x727da0
    	Statistics:
    		No. of Reads: 			4
    		No. of Writes:			3
    		No. of Buffered Writes:		0
    		No. of Accepts:			0
    		No. of High Water Events:		0
    		No. of Low Water Events:		0
            Added to persistent pool:         Jun 24 09:09:56
    MaxScale>

The information Username, Protocol, Server Status are not
always relevant, and will not be shown when they are null.  
The time the DCB was added to the persistent pool is only shown
for a DCB that is in a persistent pool.

<a name="filters"></a> 
# Working with Filters

Filters allow the request contents and result sets from a database to be modified for a client connection, pipelines of filters can be created between the client connection and MaxScale router modules.

## What Filters Are Configured?

Filters are configured in the configuration file for MaxScale, they are given names and may be included in the definition of a service. The list filters command can be used to determine which filters are defined.

    MaxScale> list filters
    Filters
    --------------------+-----------------+----------------------------------------
    Filter              | Module          | Options
    --------------------+-----------------+----------------------------------------
    counter             | testfilter      | 
    QLA                 | qlafilter       | /tmp/QueryLog 
    Replicate           | tee             | 
    QLA_BLR             | qlafilter       | /tmp/QueryLog.blr0 
    regex               | regexfilter     | 
    MySQL5.1            | regexfilter     | 
    top10               | topfilter       | 
    --------------------+-----------------+----------------------------------------
    MaxScale> 

## Retrieve Details Of A Filter Configuration

The command show filter can be used to display information related to a particular filter.

    MaxScale> show filter QLA
    Filter 0x719460 (QLA)
    	Module:	qlafilter
    	Options:	/tmp/QueryLog 
    		Limit logging to connections from 	127.0.0.1
    		Include queries that match		select.*from.*user.*where
    MaxScale> 

## Filter Usage

The show session command will include details for each of the filters in use within a session.  First use list sessions or list clients to find the session of interest and then run the show session command

    MaxScale> list clients
    Client Connections
    -----------------+------------+----------------------+------------
     Client          | DCB        | Service              | Session
    -----------------+------------+----------------------+------------
     127.0.0.1       |   0x7361a0 | Split Service        |   0x736680
     127.0.0.1       |   0x737ec0 | Plumbing             |   0x7382b0
     127.0.0.1       |   0x73ab20 | DigitalOcean         |   0x73ad90
     127.0.0.1       |   0x7219e0 | CLI                  |   0x721bd0
    -----------------+------------+----------------------+------------
    MaxScale> show session 0x736680
    Session 0x736680
    	State:    		Session ready for routing
    	Service:		Split Service (0x719f60)
    	Client DCB:		0x7361a0
    	Client Address:		127.0.0.1
    	Connected:		Thu Jun 26 10:10:44 2014
    	Filter: top10
    		Report size			10
    		Logging to file /tmp/Query.top10.1.
    		Current Top 10:
    		1 place:
    			Execution time: 23.826 seconds
    			SQL: select sum(salary), year(from_date) from salaries s, (select distinct year(from_date) as y1 from salaries) y where (makedate(y.y1, 1) between s.from_date and s.to_date) group by y.y1 ("1988-08-01?
    		2 place:
    			Execution time: 5.251 seconds
    			SQL: select d.dept_name as "Department", y.y1 as "Year", count(*) as "Count" from departments d, dept_emp de, (select distinct year(from_date) as y1 from dept_emp order by 1) y where d.dept_no = de.dept_no and (makedate(y.y1, 1) between de.from_date and de.to_date) group by y.y1, d.dept_name order by 1, 2
    		3 place:
    			Execution time: 2.903 seconds
    			SQL: select year(now()) - year(birth_date) as age, gender, avg(salary) as "Average Salary" from employees e, salaries s where e.emp_no = s.emp_no and ("1988-08-01"  between from_date AND to_date) group by year(now()) - year(birth_date), gender order by 1,2
    		4 place:
    			Execution time: 2.138 seconds
    			SQL: select dept_name as "Department", sum(salary) / 12 as "Salary Bill" from employees e, departments d, dept_emp de, salaries s where e.emp_no = de.emp_no and de.dept_no = d.dept_no and ("1988-08-01"  between de.from_date AND de.to_date) and ("1988-08-01"  between s.from_date AND s.to_date) and s.emp_no = e.emp_no group by dept_name order by 1
    		5 place:
    			Execution time: 0.839 seconds
    			SQL: select dept_name as "Department", avg(year(now()) - year(birth_date)) as "Average Age", gender from employees e, departments d, dept_emp de where e.emp_no = de.emp_no and de.dept_no = d.dept_no and ("1988-08-01"  between from_date AND to_date) group by dept_name, gender
    		6 place:
    			Execution time: 0.662 seconds
    			SQL: select year(hire_date) as "Hired", d.dept_name, count(*) as "Count" from employees e, departments d, dept_emp de where de.emp_no = e.emp_no and de.dept_no = d.dept_no group by d.dept_name, year(hire_date)
    		7 place:
    			Execution time: 0.286 seconds
    			SQL: select moves.n_depts As "No. of Departments", count(moves.emp_no) as "No. of Employees" from (select de1.emp_no as emp_no, count(de1.emp_no) as n_depts from dept_emp de1 group by de1.emp_no) as moves group by moves.n_depts order by 1
    		8 place:
    			Execution time: 0.248 seconds
    			SQL: select year(now()) - year(birth_date) as age, gender, count(*) as "Count" from employees group by year(now()) - year(birth_date), gender order by 1,2@
    		9 place:
    			Execution time: 0.182 seconds
    			SQL: select year(hire_date) as "Hired", count(*) as "Count" from employees group by year(hire_date)
    		10 place:
    			Execution time: 0.169 seconds
    			SQL: select year(hire_date) - year(birth_date) as "Age", count(*) as Count from employees group by year(hire_date) - year(birth_date) order by 1
    MaxScale> 

The data displayed varies from filter to filter, the example above is the top filter. This filter prints a report of the current top queries at the time the show session command is run.

<a name="monitors"></a> 
# Working With Monitors

Monitors are used to monitor the state of databases within MaxScale in order to supply information to other modules, specifically the routers within MaxScale.

## What Monitors Are Running?

To see what monitors are running within MaxScale use the list monitors command.

    MaxScale> list monitors
    +----------------------+---------------------
    | Monitor              | Status
    +----------------------+---------------------
    | MySQL Monitor        | Running
    +----------------------+---------------------
    MaxScale>

## Details Of A Particular Monitor

To see the details of a particular monitor use the show monitor command.

    MaxScale> show monitor "MySQL Monitor"
    Monitor: 0x71c370
    	Name:		MySQL Monitor
    	Monitor running
    	Sampling interval:	10000 milliseconds
    	MaxScale MonitorId:	24209641
    	Replication lag:	disabled
    	Monitored servers:	127.0.0.1:3306, 127.0.0.1:3307, 127.0.0.1:3308, 127.0.0.1:3309
    MaxScale> 

## Controlling Replication Heartbeat

Some monitors provide a replication heartbeat mechanism that monitors the delay for data that is replicated from a master to slaves in a tree structured replication environment. This can be enabled or disabled using the commands enable heartbeat and disable heartbeat.

    MaxScale> disable heartbeat "MySQL Monitor"
    MaxScale> enable heartbeat "MySQL Monitor"
    MaxScale> 

Please note that changes made via this interface will not persist across restarts of MaxScale. To make a permanent change edit the maxscale.cnf file.

Enabling the replication heartbeat mechanism will add the display of heartbeat information in the show server output

    MaxScale> show server server4
    Server 0x719800 (server4)
    	Server:			127.0.0.1
    	Status:               	Slave, Running
    	Protocol:			MySQLBackend
    	Port:				3309
    	Server Version:		5.5.25-MariaDB-log
    	Node Id:			4
    	Number of connections:	0
    	Current no. of conns:	0
    MaxScale> enable heartbeat "MySQL Monitor"
    MaxScale> show server server4
    Server 0x719800 (server4)
    	Server:			127.0.0.1
    	Status:               	Slave, Running
    	Protocol:			MySQLBackend
    	Port:				3309
    	Server Version:		5.5.25-MariaDB-log
    	Node Id:			4
    	Slave delay:			0
    	Last Repl Heartbeat:		Thu Jun 26 17:04:58 2014
    	Number of connections:	0
    	Current no. of conns:	0
    MaxScale> 

## Shutting Down A Monitor

A monitor may be shutdown using the shutdown monitor command. This allows for manual control of the status of servers using the set server and clear server commands. 

    MaxScale> shutdown monitor "MySQL Monitor"
    MaxScale> list monitors
    +----------------------+---------------------
    | Monitor              | Status
    +----------------------+---------------------
    | MySQL Monitor        | Stopped
    +----------------------+---------------------
    MaxScale> 

## Restarting A Monitor

A monitor that has been shutdown may be restarted using the restart monitor command.

    MaxScale> restart monitor "MySQL Monitor"
    MaxScale> show monitor "MySQL Monitor"
    Monitor: 0x71a310
    	Name:		MySQL Monitor
    	Monitor running
    	Sampling interval:	10000 milliseconds
    	MaxScale MonitorId:	24201552
    	Replication lag:	enabled
    	Monitored servers:	127.0.0.1:3306, 127.0.0.1:3307, 127.0.0.1:3308, 127.0.0.1:3309
    MaxScale> 

<a name="statuscommands"></a> 
# MaxScale Status Commands

A number of commands exists that enable the internal MaxScale status to be revealed, these commands give an insight to how MaxScale is using resource internally and are used to allow the tuning process to take place.

## MaxScale Thread Usage

MaxScale uses a number of threads, as defined in the MaxScale configuration file, to execute the processing of requests received from clients and the handling of responses. The show threads command can be used to determine what each thread is currently being used for.

    MaxScale> show threads
    Polling Threads.
    Historic Thread Load Average: 1.00.
    Current Thread Load Average: 1.00.
    15 Minute Average: 0.48, 5 Minute Average: 1.00, 1 Minute Average: 1.00
    Pending event queue length averages:
    15 Minute Average: 0.90, 5 Minute Average: 1.83, 1 Minute Average: 2.00
     ID | State      | # fds  | Descriptor       | Running  | Event
    ----+------------+--------+------------------+----------+---------------
      0 | Processing |      1 | 0xf55a70         | <  100ms | IN|OUT
      1 | Processing |      1 | 0xf49ba0         | <  100ms | IN|OUT
      2 | Processing |      1 | 0x7f54c0030d00   | <  100ms | IN|OUT
    MaxScale>

The resultant output returns data as to the average thread utilization for the past minutes 5 minutes and 15 minutes. It also gives a table, with a row per thread that shows what DCB that thread is currently processing events for, the events it is processing and how long, to the nearest 100ms has been send processing these events.

## The Event Queue

At the core of MaxScale is an event driven engine that is processing network events for the network connections between MaxScale and client applications and MaxScale and the backend servers. It is possible to see the event queue using the show eventq command. This will show the events currently being executed and those that are queued for execution.

    MaxScale> show eventq
    Event Queue.
    DCB              | Status     | Processing Events  | Pending Events
    -----------------+------------+--------------------+-------------------
    0x1e22f10        | Processing | IN|OUT             |                   
    MaxScale>

The output of this command gives the DCB’s that are currently in the event queue, the events queued for that DCB, and events that are being processed for that DCB.

## The Housekeeper Tasks

Internally MaxScale has a housekeeper thread that is used to  perform periodic tasks, it is possible to use the command show tasks to see what tasks are outstanding within the housekeeper.

    MaxScale> show tasks
    Name                      | Type     | Frequency | Next Due
    --------------------------+----------+-----------+-------------------------
    Load Average              | Repeated | 10        | Wed Nov 19 15:10:51 2014
    MaxScale>

<a name="admincommands"></a> 
# Administration Commands

## What Modules Are In use?

In order to determine what modules are in use, and the version and status of those modules the list modules command can be used.

    MaxScale> list modules
    Modules.
    ----------------+-------------+---------+-------+-------------------------
    Module Name     | Module Type | Version | API   | Status
    ----------------+-------------+---------+-------+-------------------------
    tee             | Filter      | V1.0.0  | 1.1.0 | Alpha
    qlafilter       | Filter      | V1.1.1  | 1.1.0 | Alpha
    topfilter       | Filter      | V1.0.1  | 1.1.0 | Alpha
    MySQLBackend    | Protocol    | V2.0.0  | 1.0.0 | Alpha
    maxscaled       | Protocol    | V1.0.0  | 1.0.0 | Alpha
    telnetd         | Protocol    | V1.0.1  | 1.0.0 | Alpha
    MySQLClient     | Protocol    | V1.0.0  | 1.0.0 | Alpha
    mysqlmon        | Monitor     | V1.2.0  | 1.0.0 | Alpha
    readconnroute   | Router      | V1.0.2  | 1.0.0 | Alpha
    readwritesplit  | Router      | V1.0.2  | 1.0.0 | Alpha
    debugcli        | Router      | V1.1.1  | 1.0.0 | Alpha
    cli             | Router      | V1.0.0  | 1.0.0 | Alpha
    ----------------+-------------+---------+-------+-------------------------
    MaxScale>

This command provides important version information for the module. Each module has two versions; the version of the module itself and the version of the module API that it supports. Also included in the output is the status of the module, this may be "In Development", “Alpha”, “Beta”, “GA” or “Experimental”.

## Enabling syslog and maxlog logging

MaxScale can log messages to syslog, to a log file or to both. The approach can be set in the config file, but can also be changed from maxadmin. Syslog logging is identified by *syslog* and file logging by *maxlog*.

    MaxScale> enable syslog
    MaxScale> disable maxlog

**NOTE** If you disable both, then you will see no messages at all.

## Rotating the log file

MaxScale logs messages to a log file in the log directory of MaxScale. As the log file grows continuously, it is recommended to periodically rotate it. When rotated, the current log file will be closed and a new one with a new name opened. The log file name contain a sequence number, which is incremented each time the log is rotated.

There are two ways for rotating the log - *flush log maxscale* and *flush logs* - and the result is identical. The two alternatives are due to historical reasons; earlier MaxScale had several different log files.

    MaxScale> flush log maxscale
    MaxScale>
    The flush logs command may be used to rotate all logs with a single command.
    MaxScale> flush logs
    MaxScale>

## Change MaxScale Logging Options

From version 1.3 onwards, MaxScale has a single log file where messages of various priority (aka severity) are logged. Consequently, you no longer enable or disable log files but log priorities. The priorities are the same as those of syslog and the ones that can be enabled or disabled are *debug*, *info*, *notice* and *warning*. *Error* and any more severe messages can not be disabled.

    MaxScale> enable log-priority info
    MaxScale> disable log-priority notice
    MaxScale>

Please note that changes made via this interface will not persist across restarts of MaxScale. To make a permanent change edit the maxscale.cnf file.

## Reloading The Configuration

A command, reload config, is available that will cause MaxScale to reload the maxscale.cnf configuration file.

## Shutting Down MaxScale

The MaxScale server may be shutdown using the shutdown maxscale command.

<a name="connections"></a> 
# Configuring MaxScale to Accept MaxAdmin Connections

In order to allow the use of the MaxAdmin client interface the service must be added to the maxscale.cnf file of the Maxscale server. The CLI service itself must be added and a listener for the maxscaled protocol.

The default entries required are shown below.

    [CLI]
    type=service
    router=cli
    
    [CLI Listener]
    type=listener
    service=CLI
    protocol=maxscaled
    address=localhost
    port=6603

Note that this uses the default port of 6603 and confines the connections to localhost connections only. Remove the address= entry to allow connections from any machine on your network. Changing the port from 6603 will mean that you must allows pass a -p option to the MaxAdmin command.

<a name="tuning"></a> 
# Tuning MaxScale

The way that MaxScale does it’s polling is that each of the polling threads, as defined by the threads parameter in the configuration file, will call epoll_wait to obtain the events that are to be processed. The events are then added to a queue for execution. Any thread can read from this queue, not just the thread that added the event. 

Once the thread has done an epoll call with no timeout it will either do an epoll_wait call with a timeout or it will take an event from the queue if there is one. These two new parameters affect this behavior.

The first parameter, which may be set by using the non_blocking_polls option in the configuration file, controls the number of epoll_wait calls that will be issued without a timeout before MaxScale will make a call with a timeout value. The advantage of performing a call without a timeout is that the kernel treats this case as different and will not rescheduled the process in this case. If a timeout is passed then the system call will cause the MaxScale thread to be put back in the scheduling queue and may result in lost CPU time to MaxScale. Setting the value of this parameter too high will cause MaxScale to consume a lot of CPU when there is infrequent work to be done. The default value of this parameter is 3.

This parameter may also be set via the maxadmin client using the command set nbpolls <number>.

The second parameter is the maximum sleep value that MaxScale will pass to epoll_wait. What normally happens is that MaxScale will do an epoll_wait call with a sleep value that is 10% of the maximum, each time the returns and there is no more work to be done MaxScale will increase this percentage by 10%. This will continue until the maximum value is reached or until there is some work to be done. Once the thread finds some work to be done it will reset the sleep time it uses to 10% of the maximum. 

The maximum sleep time is set in milliseconds and can be placed in the [maxscale] section of the configuration file with the poll_sleep parameter. Alternatively it may be set in the maxadmin client using the command set pollsleep <number>. The default value of this parameter is 1000.

Setting this value too high means that if a thread collects a large number of events and adds to the event queue, the other threads might not return from the epoll_wait calls they are running for some time resulting in less overall performance. Setting the sleep time too low will cause MaxScale to wake up too often and consume CPU time when there is no work to be done.

The show epoll command can be used to see how often we actually poll with a timeout, the first two values output are significant. Also the "Number of wake with pending events" is a good measure. This is the count of the number of times a blocking call returned to find there was some work waiting from another thread. If the value is increasing rapidly reducing the maximum sleep value and increasing the number of non-blocking polls should help the situation.

    MaxScale> show epoll
    Number of epoll cycles: 			534
    Number of epoll cycles with wait: 	10447
    Number of read events:   			35
    Number of write events: 			1988
    Number of error events: 			0
    Number of hangup events:			1
    Number of accept events:			3
    Number of times no threads polling:	5
    Current event queue length:		1
    Maximum event queue length:		2
    Number of DCBs with pending events:	0
    Number of wakeups with pending queue:	0
    No of poll completions with descriptors
    	No. of descriptors	No. of poll completions.
    	 1			534
    	 2			0
    	 3			0
    	 4			0
    	 5			0
    	 6			0
    	 7			0
    	 8			0
    	 9			0
    	>= 10			0
    MaxScale>

If the "Number of DCBs with pending events" grows rapidly it is an indication that MaxScale needs more threads to be able to keep up with the load it is under.

The show threads command can be used to see the historic average for the pending events queue, it gives 15 minute, 5 minute and 1 minute averages. The load average it displays is the event count per poll cycle data. An idea load is 1, in this case MaxScale threads and fully occupied but nothing is waiting for threads to become available for processing.

The show eventstats command can be used to see statistics about how long events have been queued before processing takes place and also how long the events took to execute once they have been allocated a thread to run on. 

    MaxScale> show eventstats
    Event statistics.
    Maximum queue time:		 	 2600ms
    Maximum execution time:		 1600ms
    Maximum event queue length:	    3
    Current event queue length:	    3
                   |    Number of events
    Duration       | Queued     | Executed
    ---------------+------------+-----------
     < 100ms       | 107        | 461       
      100 -  200ms | 958        | 22830     
      200 -  300ms | 20716      | 2545      
      300 -  400ms | 3284       | 253       
      400 -  500ms | 505        | 45        
      500 -  600ms | 66         | 73        
      600 -  700ms | 116        | 169       
      700 -  800ms | 319        | 185       
      800 -  900ms | 382        | 42        
      900 - 1000ms | 95         | 31        
     1000 - 1100ms | 63         | 7         
     1100 - 1200ms | 18         | 4         
     1200 - 1300ms | 8          | 2         
     1300 - 1400ms | 6          | 0         
     1400 - 1500ms | 1          | 1         
     1500 - 1600ms | 3          | 1         
     1600 - 1700ms | 2          | 1         
     1700 - 1800ms | 2          | 0         
     1800 - 1900ms | 0          | 0         
     1900 - 2000ms | 1          | 0         
     2000 - 2100ms | 0          | 0         
     2100 - 2200ms | 0          | 0         
     2200 - 2300ms | 0          | 0         
     2300 - 2400ms | 0          | 0         
     2400 - 2500ms | 0          | 0         
     2500 - 2600ms | 0          | 0         
     2600 - 2700ms | 1          | 0         
     2700 - 2800ms | 0          | 0         
     2800 - 2900ms | 0          | 0         
     2900 - 3000ms | 0          | 0         
     > 3000ms      | 0          | 0         
    MaxScale> 

The statics are defined in 100ms buckets, with the count of the events that fell into that bucket being recorded.

