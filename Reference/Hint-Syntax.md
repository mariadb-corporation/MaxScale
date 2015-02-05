# Hint Syntax

Use either ’-- ’ (notice the whitespace) or ’#’ after the semicolon or ’/* .. */’ before
the semicolon.

The MySQL manual doesn’t specify if comment blocks, i.e. ’/* .. */’, should contain a w
hitespace character before or after the tags.

All hints must start with the ’maxscale tag’:
	-- maxscale <hint>
	
The hints right now have two types, ones that route to a server and others that contain
name-value pairs.

Routing queries to a server:
-- maxscale route to [master | slave | server <server name>]

The name of the server is the same as in MaxScale.cnf

Creating a name-value pair:
-- maxscale <param>=<value>

Currently the only accepted parameter is
’max_slave_replication_lag’

Hints can be either single-use hints, which makes them affect only one query, or named
hints, which can be pushed on and off a stack of active hints.

Defining named hints:
-- maxscale <hint name> prepare <hint content>

Pushing a hint onto the stack:
-- maxscale <hint name> begin

Popping the topmost hint off the stack:
-- maxscale end

You can define and activate a hint in a single command using the following:
-- maxscale <hint name> begin <hint content>

You can also push anonymous hints onto the stack which are only used as long as they are on the stack:
-- maxscale begin <hint content>