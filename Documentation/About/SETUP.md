Installation and startup

Untar the binary distribution in the desired location,
e.g. /usr/local/mariadb-maxscale

Alternatively build from the source code using the instructions
in the [Building MaxScale from Source Code](../Getting-Started/Building-MaxScale-from-Source-Code.md) document.

You can start MaxScale using `service maxscale start` or `systemctl start maxscale` if you installed the init.d scripts
or by manually starting the process from the bin folder of the installation directory.

Configuration

You need to create or edit the MaxScale.cnf file in the /etc folder.
Define the services you wish to provide, the set of server definitions
you require, with the addresses and ports of those servers and also 
define the listening ports for your various services.

In order to view the internal activity of MaxScale you can either use 
the maxadmin client interface with the cli routing module or telnet to
the port defined for the telnet listener. Initially you may login with
the user name of "admin" and the password "mariadb". Once connected type
help for an overview of the commands and help <command> for the more
detailed help on commands. Use the add user command to add a new user,
this will also remove the admin/mariadb user.
