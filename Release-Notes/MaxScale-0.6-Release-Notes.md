# MariaDB MaxScale 0.6 Alpha Release Notes

0.6 Alpha

This document details the changes in version 0.6 since the release of the 0.5 alpha of the MaxScale product. The 0.6 version is merely a set of bug fixes based on the previous 0.5 version.

# Bug Fixes

A number of bug fixes have been applied between the 0.5 alpha and this alpha release. The table below lists the bugs that have been resolved. The details for each of these may be found in bugs.skysql.com.

<table>
  <tr>
    <td>ID</td>
    <td>Summary</td>
  </tr>
  <tr>
    <td>423</td>
    <td>The new "version_string" parameter has been added to service section.
This allows a specific version string to be set for each service, this version string is used in the MySQL handshake from MaxScale to clients and is reported as the server version to clients.

The version_string is optional, the default value will be taken from the embedded MariaDB library which supplies the parser to MaxScale.</td>
  </tr>
  <tr>
    <td>418</td>
    <td>Statements are not routed to master if a transaction is started implicitly by setting autocommit=0. In such cases statements were previously routed as if they were not part of a transaction.

This fix changes the behavior so that is autocommit is disabled, all statements are routed to the master and in case of session variable updates, to both master and slave.</td>
  </tr>
</table>


