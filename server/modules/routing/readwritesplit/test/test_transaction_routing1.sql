use test; -- in both
drop table if exists t1;
create table t1 (id integer);
insert into t1 values(1); -- in master
commit;
select count(*) from t1; -- in slave
