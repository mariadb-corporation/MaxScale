Installation and startup

Untar the binary distribution in the desired location,
e.g. /usr/local/mariadb

Alternatively build from the source code using the instructions
in the README file and execute make install.

Simply set the environment variable MAXSCALE_HOME to point to the
MaxScale directory, found inside the path into which the files have been copied,
e.g. MAXSCALE_HOME=/usr/local/mariadb-maxscale

Also you will need to optionaly set LD_LIBRARY_PATH to include the 'lib' folder,
found inside the path into which the files have been copied,
e.g. LD_LIBRARY_PATH=/usr/local/mariadb-maxscale/lib

Because we need the libmysqld library for parsing we must create a
valid my.cnf file to enable the library to be used. Copy the my.cnf
to $MAXSCALE_HOME/mysql/my.cnf.

To start MaxScale execute the command 'maxscale' from the bin folder,
e.g. /usr/local/mariadb-maxscale/bin/maxscale

Configuration

You need to edit the file MaxScale.cnf in $MAXSCALE_HOME/etc, you should
define the set of server definitions you require, with the addresses
and ports of those servers. Also define the listening ports for your
various services.

In order to view the internal activity of the gateway you can telnet to
the port defined for the telnet listener. Initially you may login with
the user name of "admin" and the password "mariadb". Once connected type
help for an overview of the commands and help <command> for the more
detailed help on commands. Use the add user command to add a new user,
this will also remove the admin/mariadb user.

