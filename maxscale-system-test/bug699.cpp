/**
 * @file bug699.cpp regression case for bug 699 ( "rw-split sensitive to order of terms in field list of
 * SELECT (round 2)" )
 *
 * - compare @@hostname from "select  @@wsrep_node_name, @@hostname" and "select  @@hostname,
 *@@wsrep_node_name"
 * - comapre @@server_id from "select  @@wsrep_node_name, @@server_id" and "select  @@server_id,
 *@@wsrep_node_name"
 */

/*
 *  Kolbe Kegel 2015-01-16 18:38:15 UTC
 *  I opened bug #509 some time ago, but that bug was handled specifically for the case of last_insert_id().
 * The crux of that bug still exists in 1.0.4 GA:
 *
 *  [root@max1 ~]# mysql -h 127.0.0.1 -P 4006 -u maxuser -pmaxpwd -e 'select @@hostname, @@wsrep_node_name;
 * select @@wsrep_node_name, @@hostname;'
 +------------+-------------------+
 | @@hostname | @@wsrep_node_name |
 +------------+-------------------+
 | db2        | db2               |
 +------------+-------------------+
 +-------------------+------------+
 | @@wsrep_node_name | @@hostname |
 +-------------------+------------+
 | db3               | db3        |
 +-------------------+------------+
 |
 |  In a single connection, fetching the values of the same two system variables results in the query being
 |routed differently depending on the order of the variables.
 |
 |  Is there some set of variables that should always be routed to the master for some reason? If so, that
 |should be documented.
 |
 |  Regardless, the order of terms in the SELECT list should not have any effect on query routing.
 |  Comment 1 Vilho Raatikka 2015-01-16 22:33:38 UTC
 |  @@wsrep_node_name can be resolved only in backend, MaxScale doesn't know about it. As a consequence, Since
 |MaxScale doesn't know what it is it takes the safe bet and routes it to master.
 |  Comment 2 Kolbe Kegel 2015-01-16 22:35:13 UTC
 |  What does "(In reply to comment #1)
 |  > @@wsrep_node_name can be resolved only in backend, MaxScale doesn't know
 |  > about it. As a consequence, Since MaxScale doesn't know what it is it takes
 |  > the safe bet and routes it to master.
 |
 |  What do you mean by "resolved only in backend" and "MaxScale doesn't know about it"?
 |
 |  How is @@wsrep_node_name different in that respect from any other server variable that could be different
 |on any given backend?
 |  Comment 3 Vilho Raatikka 2015-01-16 23:01:31 UTC
 |  (In reply to comment #2)
 |  > What does "(In reply to comment #1)
 |  > > @@wsrep_node_name can be resolved only in backend, MaxScale doesn't know
 |  > > about it. As a consequence, Since MaxScale doesn't know what it is it takes
 |  > > the safe bet and routes it to master.
 |  >
 |  > What do you mean by "resolved only in backend" and "MaxScale doesn't know
 |  > about it"?
 |  >
 |  > How is @@wsrep_node_name different in that respect from any other server
 |  > variable that could be different on any given backend?
 |
 |  MaxScale doesn't know that there is such system variable as @@wsrep_node_name. In my understanding the
 |reason is that the embedded MariaDB server doesn't have Galera's patch. Thus the variable is unknown.
 |  Comment 4 Kolbe Kegel 2015-01-16 23:03:13 UTC
 |  > >
 |  > > How is @@wsrep_node_name different in that respect from any other server
 |  > > variable that could be different on any given backend?
 |  >
 |  > MaxScale doesn't know that there is such system variable as
 |  > @@wsrep_node_name. In my understanding the reason is that the embedded
 |  > MariaDB server doesn't have Galera's patch. Thus the variable is unknown.
 |
 |  Ahh, right. That sounds familiar. But it's still quite strange that the order of variables in the SELECT
 |statement is meaningful, isn't it?
 |  Comment 5 Vilho Raatikka 2015-01-16 23:09:47 UTC
 |  (In reply to comment #4)
 |  > > >
 |  > > > How is @@wsrep_node_name different in that respect from any other server
 |  > > > variable that could be different on any given backend?
 |  > >
 |  > > MaxScale doesn't know that there is such system variable as
 |  > > @@wsrep_node_name. In my understanding the reason is that the embedded
 |  > > MariaDB server doesn't have Galera's patch. Thus the variable is unknown.
 |  >
 |  > Ahh, right. That sounds familiar. But it's still quite strange that the
 |  > order of variables in the SELECT statement is meaningful, isn't it?
 |
 |  The effectiveness of atrribute order in SELECT clause is a bug which is fixed in
 |http://bugs.skysql.com/show_bug.cgi?id=694 .
 |
 |  MaxScale's inability to detect different system variables is slightly problematic as well but haven't
 |really concentrated on finding a decent solution to it yet. It might, however, be necessary.
 |  Comment 6 Vilho Raatikka 2015-01-16 23:55:56 UTC
 |  Attribute order effectiveness is fixed in http://bugs.skysql.com/show_bug.cgi?id=694
 |
 |  Inability to detect Galera's system variables is hard to overcome since Galera patch doesn't work with
 |embedded library and embedded library doesn't know Galera's system variables.
 |  Comment 7 Vilho Raatikka 2015-01-17 09:38:09 UTC
 |  Appeared that MariaDB parsing end result depends on the order of [known,unknown] system variable pair in
 |the query.
 |
 |  There was similar-looking bug in query_classifier before which hide this one. However, debugging and
 |examining the resulting thd and lex for the following queries shows that thd->free_list is non-empty if
 |@@hostname (known variable) is before @@wsrep_node_name. If @@wsrep_node_name is first on the attribute list
 |the resulting thd->free_list==NULL.
 |  In the former case resulting query type is QUERY_TYPE_SYSVAR_READ (routed to slaves) and in the latter
 |case it is unknown (routed to master).
 |
 |  1. select  @@wsrep_node_name, @@hostname;
 |  2. select  @@hostname, @@wsrep_node_name;
 |
 |  Both queries produce similar response but routing them to master only limits scalability.
 |  Comment 8 Mark Riddoch 2015-01-28 08:39:25 UTC
 |  Raised this with the server team as the issue is related to the behaviour of the parser in the embedded
 |server. System variables are resolved at parse time, unknown variables result in a parse error normally,
 |however this order dependency is slightly puzzling.
 |  Comment 9 Mark Riddoch 2015-02-13 10:06:08 UTC
 |  Hi Sergei,
 |
 |  we have an interesting bug in MaxScale related to parsing. If we try to parse the query
 |
 |  select @@wsrep_node_name;
 |
 |  Using the embedded server we link with we do not get a parse tree. A select of other system variables
 |works. I guess the parser is resolving the name of the variable rather than leaving it to the execution
 |phase. Since we do not have Galera this variable is unknown. What is even more strange is that if we have a
 |query of the form
 |
 |  select @@hostname, @@wsrep_node_name;
 |
 |  We do get a parse tree, but reversing the order of the select we again fail to get a parse tree with the
 |query
 |
 |  select @@wsrep_node_name, @@hostname;
 |
 |  For our purposes we would ideally like to disable the resolving of the variable name at parse time, since
 |that would give us flexibility with regard to new variables being introduced in the servers. Do you know if
 |this is possible or if there is some easy fix we can do to the MariaDB parser that will help us here?
 |
 |  For your reference the MaxScale bug report can be found here http://bugs.skysql.com/show_bug.cgi?id=699
 |
 |
 |  Thanks
 |  Mark
 |  Comment 10 Mark Riddoch 2015-02-13 10:08:34 UTC
 |  Hi, Mark!
 |
 |  On Jan 28, Mark Riddoch wrote:
 |  > Hi Sergei,
 |  >
 |  > we have an interesting bug in MaxScale related to parsing. If we try
 |  > to parse the query
 |  >
 |  > select @@wsrep_node_name;
 |  >
 |  > Using the embedded server we link with we do not get a parse tree. A
 |  > select of other system variables works. I guess the parser is
 |  > resolving the name of the variable rather than leaving it to the
 |  > execution phase. Since we do not have Galera this variable is unknown.
 |
 |  Right... That would be *very* difficult to fix, it'd require a pretty
 |  serious refactoring to get this out of the parser.
 |
 |  > What is even more strange is that if we have a query of the form
 |  >
 |  > select @@hostname, @@wsrep_node_name;
 |  >
 |  > We do get a parse tree, but reversing the order of the select we again
 |  > fail to get a parse tree with the query
 |  >
 |  > select @@wsrep_node_name, @@hostname;
 |
 |  That depends on what you call a "parse tree". Items of the select clause
 |  are stored in the thd->lex->current_select->item_list.
 |
 |  For the first query, the list have 1 element, Item_func_get_system_var
 |  for @@hostname.
 |
 |  For the second query the list has 0 elements. In both cases, I've
 |  examined the list in the debugger after MYSQLparse() returned.
 |
 |  So apparently the parsing as aborted as soon as unknown variable is
 |  encountered.
 |
 |  > For our purposes we would ideally like to disable the resolving of the
 |  > variable name at parse time, since that would give us flexibility with
 |  > regard to new variables being introduced in the servers. Do you know
 |  > if this is possible or if there is some easy fix we can do to the
 |  > MariaDB parser that will help us here?
 |
 |  I don't see how you can do that from MaxScale.
 |  This looks like something that has to be fixed in the server.
 |  Perhaps - not sure - it'd be possible to introduce some "mode" for the
 |  parser where it doesn't check for valid variable names in certain
 |  contextes.
 |
 |  Regards,
 |  Sergei
 |
 */



#include <iostream>
#include "testconnections.h"

const char* sel1 = "select  @@wsrep_node_name, @@hostname";
const char* sel2 = "select  @@hostname, @@wsrep_node_name";

const char* sel3 = "select  @@wsrep_node_name, @@server_id";
const char* sel4 = "select  @@server_id, @@wsrep_node_name";

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    Test->maxscales->connect_maxscale(0);

    Test->tprintf("Trying \n");

    char serverid1[1024];
    char serverid2[1024];

    if ((
            find_field(Test->maxscales->conn_rwsplit[0],
                       sel3,
                       "@@server_id",
                       &serverid1[0])
            != 0 ) || (
            find_field(Test->maxscales->conn_rwsplit[0],
                       sel4,
                       "@@server_id",
                       &serverid2[0])
            != 0 ))
    {
        Test->add_result(1, "@@server_id field not found!!\n");
        delete Test;
        exit(1);
    }
    else
    {
        Test->tprintf("'%s' to RWSplit gave @@server_id %s\n", sel3, serverid1);
        Test->tprintf("'%s' directly to master gave @@server_id %s\n", sel4, serverid2);
        Test->add_result(strcmp(serverid1, serverid2),
                         "server_id are different depending in which order terms are in SELECT\n");
    }

    if ((
            find_field(Test->maxscales->conn_rwsplit[0],
                       sel1,
                       "@@hostname",
                       &serverid1[0])
            != 0 ) || (
            find_field(Test->maxscales->conn_rwsplit[0],
                       sel2,
                       "@@hostname",
                       &serverid2[0])
            != 0 ))
    {
        Test->add_result(1, "@@hostname field not found!!\n");
        delete Test;
        exit(1);
    }
    else
    {
        Test->tprintf("'%s' to RWSplit gave @@hostname %s\n", sel1, serverid1);
        Test->tprintf("'%s' to RWSplit gave @@hostname %s\n", sel2, serverid2);
        Test->add_result(strcmp(serverid1, serverid2),
                         "hostname are different depending in which order terms are in SELECT\n");
    }

    Test->maxscales->close_maxscale_connections(0);
    Test->check_maxscale_alive(0);
    int rval = Test->global_result;
    delete Test;
    return rval;
}
