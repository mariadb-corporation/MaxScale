# MaxScale Nagios plugins, for Nagios 3.5.1

Massimiliano Pinto

Last Updated: 12th March 2015

## Document History

<table>
  <tr>
    <td>Date</td>
    <td>Change</td>
    <td>Who</td>
  </tr>
  <tr>
    <td>10th March 2015</td>
    <td>Initial version</td>
    <td>Massimiliano Pinto</td>
  </tr>
</table>

# MaxScale Requirements

MaxScale must be configured with 'maxscaled' protocol for the administration interface

	[AdminInterface]
	type=service
	router=cli

	[AdminListener]
	type=listener
	service=AdminInterface
	protocol=maxscaled
	port=6603

## Prepare Nagios configuration files (assuming plugins are in /usr/lib64/nagios/plugins and Nagios configuration in /etc/nagios)

	1) Copy ./nagios/plugins/check_maxscale_*.pl under /usr/lib64/nagios/plugins
	2) Copy ./nagios/plugins/maxscale_commands.cfg and server1.cfg to /etc/nagios/objects/
	3) Edit /etc/nagios/nagios.cfg

and add (just after localhost.cfg or commnads.cfg)

	cfg_file=/etc/nagios/objects/maxscale_commands.cfg
	cfg_file=/etc/nagios/objects/server1.cfg

### Please note:
- modify server IP address in server1.cfg, pointing to MaxScale server
- maxadmin executable must be in the nagios server
- default AdminInterface port is 6603
- default maxadmin executable path is /usr/local/skysql/maxscale/bin/maxadmin
	It can be changed by -m option

Example related to server1.cfg

# Check MaxScale sessions, on the remote machine.
define service{
        use                             local-service         ; Name of service template to use
        host_name                       server1
        service_description             MaxScale_sessions
        check_command                   check_maxscale_resource!6603!admin!skysql!sessions!/path_to/maxadmin
        notifications_enabled           0
        }

	4) Restart Nagios


# Nagios Plugin usage

	./check_maxscale_threads.pl -h

	MaxScale monitor checker plugin for Nagios

	Usage: check_maxscale_threads.pl [-r <resource>] [-H <host>] [-P <port>] [-u <user>] [-p <pass>] [-m <maxadmin>] [-h]

	Options:
		-r <resource>	= threads
		-h		= provide this usage message
		-H <host>	= which host to connect to
		-P <port>	= port to use
		-u <user>	= username to connect as
		-p <pass>	= password to use for <user> at <host>
		-m <maxadmin>	= /path/to/maxadmin

