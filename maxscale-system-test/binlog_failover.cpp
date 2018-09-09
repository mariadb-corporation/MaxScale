/**
 * @file binlog_failover.cpp binlog_failover Test of failover scenarion for binlog router
 *
 * - setup following configuration:
 *   - one master
 *   - two maxscale binlog machines
 *   - two slaves connected to each maxscale binlog mashine
 * - put some date via master
 * - block master
 * - stop all Maxscale machines with STOP SLAVE command
 * - check which Maxscale machine contains most recent data (let's call this machine 'most_recent_maxscale')
 * - use CHANGE MASETER on the second Maxscale machine to point it to the Maxscale machine from the previous
 *step ('most_recent_maxscale')
 * - wait until second Maxscale is in sync with 'most_recent_maxscale' (use SHOW MASTER STATUS)
 * - select new master (HOW??)
 * - set all Maxscale machines to be a slaves of new master
 */
