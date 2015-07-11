# MariaDB MaxScale 0.5 Alpha Release Notes

0.5 Alpha

This document details the changes in version 0.5 since the release of the 0.4 alpha of the MaxScale product.

# New Features

## Read/Write Splitter Routing Module

In previous versions the read/write splitter routing module has had a number of limitations on it use, in the alpha release the router now removes the most important restrictions.

### Session Commands

Session commands are those statements that make some change to the user’s login session that may cause different effects from subsequent statements executed. Since the read/write splitter executes statements on either a master server or a slave server, depending upon the statement to execute, it is important that these session modifications are executed on all connections to both slave and master servers. This is resolved in release 0.5 such that session modification commands are executed on all active connections and a single return is forward back to the client that made the request.

### Transaction Support

Transaction support has been added into this version of the read/write splitter, there is one known outstanding limitation. If autocommit  is enabled inside an active transaction it is not considered as commit in read/write splitter. Once a transaction has started all statements are routed to a master until the transaction is committed or rolled back.

## Authentication

A number of issues and shortcomings in the authentication performed by MaxScale have been resolved by this release.

### Host Considered in Authentication

Previously MaxScale did not follow the same rules as MySQL when authenticating a login request, it would always use the wildcard password entries and would not check the incoming host was allowed to connect. MaxScale now checks the incoming IP address for a connection request and verifies this against the authentication data loaded from the backend servers. The same rules are applied when choosing the password entry to authenticate with. Note however that authentication from MaxScale to the backend database will fail if the MaxScale host is not allowed to login using the matching password for the user.

### Stale Authentication Data

In previous releases of MaxScale the authentication data would be read at startup time only and would not be refreshed. Therefore if a user was added or modified in the backend server this will not be picked up by MaxScale and that user would be unable to connect via MaxScale. MaxScale now reloads user authentication data when a failure occurs and will refresh its internal tables if the data has changed in the backend. Please note that this reload process is rate limited to prevent incorrect logins to MaxScale being used for a denial of service attack on the backend servers.

### Enable Use Of "root" User

Previously MaxScale would prevent the use of the root user to login to the backend servers via MaxScale. This may be enabled on a per service basis by adding an "enable_root_user" options in the service entry to enable it in the MaxScale configuration file. This allows the use of root to be controlled on a per service basis.

## Network Support

### Unix Domain Sockets

MaxScale now supports Unix domain sockets for connecting to a local MaxScale server. The use of a Unix domain socket is controlled by adding a "socket" entry in the listener configuration entry for a service.

### Network Interface Binding

MaxScale has added the ability to bind a listener for a service to a network address via an "address" entry in the configuration file.

# Server Version

The server version reported when connected to a database via MaxScale has now been altered. This now shows the MaxScale name and version together with the backend server name. An example of this can be seen below for the 0.5 release.

-bash-4.1$ mysql -h 127.0.0.1 -P 4006 -uxxxx -pxxxxWelcome to the MariaDB monitor.  Commands end with ; or \\g.Your MySQL connection id is 22320Server version: MaxScale 0.5.0 MariaDB ServerCopyright (c) 2000, 2012, Oracle, Monty Program Ab and others.Type 'help;' or '\\h' for help. Type '\\c' to clear the current input statement.MySQL [(none)]> \\ys--------------mysql  Ver 15.1 Distrib 5.5.28a-MariaDB, for Linux (i686) using readline 5.1...Server:			MySQLServer version:		MaxScale 0.5.0 MariaDB Server...--------------MySQL [(none)]>

# Bug Fixes

A number of bug fixes have been applied between the 0.4 alpha and this alpha release. The table below lists the bugs that have been resolved. The details for each of these may be found in bugs.skysql.com.

<table>
  <tr>
    <td>ID</td>
    <td>Summary</td>
  </tr>
  <tr>
    <td>141</td>
    <td>No "delete user" command in debugcli</td>
  </tr>
  <tr>
    <td>175</td>
    <td>Buffer leak in dcb_read from Coverity run</td>
  </tr>
  <tr>
    <td>178</td>
    <td>Uninitialised variables from Coverity run</td>
  </tr>
  <tr>
    <td>179</td>
    <td>open with O_CREAT in second argument needs 3 arguments</td>
  </tr>
  <tr>
    <td>363</td>
    <td>simple_mutex "name" memory handling ...</td>
  </tr>
  <tr>
    <td>126</td>
    <td>"reload config" in debug interface causes maxscale server to segfault</td>
  </tr>
  <tr>
    <td>149</td>
    <td>It is possible to delete all maxscale users</td>
  </tr>
  <tr>
    <td>218</td>
    <td>there is no way to understand what is going on if MAXSCALE_HOME is incorrect</td>
  </tr>
  <tr>
    <td>137</td>
    <td>"show users" and "reload users" refer to very different things in debugcli</td>
  </tr>
  <tr>
    <td>154</td>
    <td>readwritesplit does not use router_options</td>
  </tr>
  <tr>
    <td>160</td>
    <td>telnetd leaks memory</td>
  </tr>
  <tr>
    <td>169</td>
    <td>Galera monitor is actually never compiled ....</td>
  </tr>
  <tr>
    <td>172</td>
    <td>Several compile errors in galera_mon.c</td>
  </tr>
  <tr>
    <td>174</td>
    <td>Resource leak in server.c</td>
  </tr>
  <tr>
    <td>176</td>
    <td>Resource leak in gw_utils.c</td>
  </tr>
  <tr>
    <td>362</td>
    <td>possible datadir_cleanup() problems ...</td>
  </tr>
  <tr>
    <td>124</td>
    <td>readconnroute does not validate router_options</td>
  </tr>
  <tr>
    <td>153</td>
    <td>MaxScale fails when max connections are exceeded</td>
  </tr>
  <tr>
    <td>133</td>
    <td>MaxScale leaves lots of "data<pid>" directories sitting around $MAXSCALE_HOME</td>
  </tr>
  <tr>
    <td>166</td>
    <td>readwritesplit causes MaxScale segfault when starting up</td>
  </tr>
  <tr>
    <td>207</td>
    <td>Quitting telnet session causes maxscale to fail</td>
  </tr>
  <tr>
    <td>161</td>
    <td>Memory leak in load_mysql_users.</td>
  </tr>
  <tr>
    <td>177</td>
    <td>Resource leak in secrets.c</td>
  </tr>
  <tr>
    <td>182</td>
    <td>On Startup logfiles are empty</td>
  </tr>
  <tr>
    <td>135</td>
    <td>MaxScale unsafely handles empty passwords in getUsers</td>
  </tr>
  <tr>
    <td>145</td>
    <td>.secret file for encrypted passwords cyclicly searched</td>
  </tr>
  <tr>
    <td>171</td>
    <td>ifndef logic in build_gateway.inc doesn't work, MARIADB_SRC_PATH from env not picked up</td>
  </tr>
  <tr>
    <td>173</td>
    <td>Resource leak in adminusers.c found by Coverity</td>
  </tr>
  <tr>
    <td>376</td>
    <td>Confusing Server Version</td>
  </tr>
  <tr>
    <td>370</td>
    <td>maxscale binary returns zero exit status on failures</td>
  </tr>
  <tr>
    <td>150</td>
    <td>telnetd listener should bind to 127.0.0.1 by default</td>
  </tr>
  <tr>
    <td>152</td>
    <td>listener configuration should support bind address</td>
  </tr>
  <tr>
    <td>373</td>
    <td>Documentation: it's not clear what privileges the maxscale user needs</td>
  </tr>
  <tr>
    <td>128</td>
    <td>Maxscale prints debug information to terminal session when run in background</td>
  </tr>
  <tr>
    <td>129</td>
    <td>MaxScale refuses to connect to server and reports nonsense error as a result</td>
  </tr>
  <tr>
    <td>147</td>
    <td>Maxscale's hashtable fails to handle deletion of entries.</td>
  </tr>
  <tr>
    <td>148</td>
    <td>users data structure's stats have incorrect values.</td>
  </tr>
  <tr>
    <td>384</td>
    <td>MaxScale crashes if backend authentication fails</td>
  </tr>
  <tr>
    <td>210</td>
    <td>Bad timing in freeing readconnrouter's dcbs cause maxscale crash</td>
  </tr>
  <tr>
    <td>403</td>
    <td>gwbuf_free doesn't protect freeing shared buffer</td>
  </tr>
  <tr>
    <td>371</td>
    <td>If router module load fails, MaxScale goes to inifinite loop</td>
  </tr>
  <tr>
    <td>385</td>
    <td>MaxScale (DEBUG-version) dasserts if backend dcb is closed in the middle of client dcb performing close_dcb</td>
  </tr>
  <tr>
    <td>386</td>
    <td>Starting MaxScale with -c pointing at existing file causes erroneous behavior</td>
  </tr>
  <tr>
    <td>209</td>
    <td>Error in backend hangs client connection</td>
  </tr>
  <tr>
    <td>194</td>
    <td>maxscale crashes at start if module load fails</td>
  </tr>
  <tr>
    <td>369</td>
    <td>typo in "QUERY_TYPE_UNKNWON"</td>
  </tr>
  <tr>
    <td>163</td>
    <td>MaxScale crashes with multiple threads</td>
  </tr>
  <tr>
    <td>162</td>
    <td>threads parameter in configuration file is not effective</td>
  </tr>
  <tr>
    <td>400</td>
    <td>hastable_get_stats returns value of uninitialized value in 'nelems'</td>
  </tr>
  <tr>
    <td>212</td>
    <td>Failing write causes maxscale to fail</td>
  </tr>
  <tr>
    <td>222</td>
    <td>Double freeing mutex corrupts log</td>
  </tr>
  <tr>
    <td>208</td>
    <td>current_connection_count is decreased multiple times per session, thus breaking load balancing logic</td>
  </tr>
  <tr>
    <td>378</td>
    <td>Misspelling maxscale section name in config file crashes maxscale</td>
  </tr>
  <tr>
    <td>399</td>
    <td>Every row in log starts with 0x0A00</td>
  </tr>
  <tr>
    <td>205</td>
    <td>MaxScale crashes due SEGFAULT because return value of dcb_read is not checked</td>
  </tr>
  <tr>
    <td>220</td>
    <td>Maxscale crash if socket listening fails in startup</td>
  </tr>
  <tr>
    <td>372</td>
    <td>Log manager hangs MaxScale if log string (mostly query length) exceeds block size</td>
  </tr>
  <tr>
    <td>397</td>
    <td>Free of uninitialised pointer if MAXSCALE_HOME is not set</td>
  </tr>
  <tr>
    <td>402</td>
    <td>gw_decode_mysql_server_handshake asserts with mysql 5.1 backend</td>
  </tr>
  <tr>
    <td>345</td>
    <td>MaxScale don't find backend servers if they are started after MaxScale</td>
  </tr>
  <tr>
    <td>406</td>
    <td>Memory leak in dcb_alloc()</td>
  </tr>
  <tr>
    <td>360</td>
    <td>MaxScale passwd option</td>
  </tr>
  <tr>
    <td>151</td>
    <td>Get parse_sql failed on array INSERT</td>
  </tr>
  <tr>
    <td>216</td>
    <td>Backend error handling doesn't update server's connection counter</td>
  </tr>
  <tr>
    <td>127</td>
    <td>MaxScale should handle out-of-date backend auth data more gracefully</td>
  </tr>
  <tr>
    <td>146</td>
    <td>"show dbusers" argument not documented</td>
  </tr>
  <tr>
    <td>125</td>
    <td>readconnroute causes maxscale server crash if no slaves are available</td>
  </tr>
  <tr>
    <td>375</td>
    <td>Tarball contains UID and maxscale base dir</td>
  </tr>
</table>


