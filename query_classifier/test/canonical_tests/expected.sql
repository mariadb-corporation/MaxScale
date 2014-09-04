select  md5(?) =?, sleep(?), rand(?);
select * from my1 where md5(?) =?;
select  md5(?) =?;
select * from my1 where md5(?) =?;
select sleep(?)
select * from tst where lname='?'
select ?,?,?,?,?,? from tst
select * from tst where fname like '?'
select * from tst where lname like '?' order by fname
insert into tst values ("?","?"),("?",?),("?","?")
drop table if exists tst
create table tst(fname varchar(30), lname varchar(30))
update tst set lname="?" where fname like '?' or lname like '?'
delete from tst where lname like '?' and fname like '?'
select ? from tst where fname='?' or lname like '?'
select ?,?,?,? from tst where name='?' or name='?' or name='?'
select count(?),count(?),count(?),count(?),count (?),count(?) from tst
