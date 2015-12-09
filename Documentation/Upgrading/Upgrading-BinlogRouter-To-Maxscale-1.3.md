# Upgrading Binlog Router to MaxScale to 1.3

This document describes upgrading the Binlog Router module to MaxScale version 1.3. The major changes can be found in the `Changelog.txt` file in the installation directory and the official release notes in the `ReleaseNotes.txt` file.

## What's new

The master server details are now provided by a **master.ini** file located in binlog directory and itcould be changed via CHANGE MASTER TO command issued via MySQL connection to MaxScale.

This file, properly filled, it's now mandatory and without that the binlog router can't connect to the master database.

Before starting binlog router after MaxScale 1.3 upgrade please add relevant informations in master.ini, example:

```
[binlog_configuration]
master_host=127.0.0.1
master_port=3308
master_user=repl
master_password=somepass
filestem=repl-bin 
```

Additionally the option ```servers=masterdb``` in the service definition is no longer required.
