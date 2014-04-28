use test;
drop table if exists t1;
create table t1 (id integer);
set autocommit=0;               -- open transaction
begin;                         
insert into t1 values(1);       -- write to master
commit;
