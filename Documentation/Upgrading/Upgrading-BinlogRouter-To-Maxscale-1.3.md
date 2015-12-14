# Upgrading Binlog Router to MaxScale to 1.3

This document describes upgrading the Binlog Router module to MaxScale version 1.3.

## What's new

The master server details are now provided with a **master.ini** file located in
the binlog directory and it can be changed using a CHANGE MASTER TO command issued
via a MySQL connection to MaxScale.

This file, properly filled, is now mandatory and without it the binlog router
cannot connect to the master database.

Before starting binlog router after MaxScale 1.3 upgrade, please add relevant
information to *master.ini*, example:

```
[binlog_configuration]
master_host=127.0.0.1
master_port=3308
master_user=repl
master_password=somepass
filestem=repl-bin
```

Additionally, the option ```servers=masterdb``` in the service definition is no
longer required.
