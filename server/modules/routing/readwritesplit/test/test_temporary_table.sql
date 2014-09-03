use test;
drop table if exists t1;
create temporary table t1 (id integer);
insert into t1 values(1);
select id from t1;
