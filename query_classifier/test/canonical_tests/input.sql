select  md5("200000foo") =10, sleep(2), rand(100);
select * from my1 where md5("110") =10;
select  md5("100foo") =10;
select * from my1 where md5("100") =10;
select sleep(2);
select * from tst where lname='Doe';
select 1,2,3,4,5,6 from tst;
select * from tst where fname like '%a%';
select * from tst where lname like '%e%' order by fname;
insert into tst values ("John","Doe"),("Plato",null),("Nietzsche","");
drop table if exists tst;
create table tst(fname varchar(30), lname varchar(30));
update tst set lname="Human" where fname like '%a%' or lname like '%a%';
delete from tst where lname like '%man%' and fname like '%ard%';
select 100 from tst where fname='10' or lname like '%100%';
select 1,20,300,4000 from tst where name='1000' or name='200' or name='30' or name='4';
select count(1),count(10),count(100),count(2),count (20),count(200) from tst;
begin;
BEGIN
BEGIN;
commit;
COMMIT;
COMMIT;
CREATE DATABASE FOO;
CREATE EVENT myevent
CREATE FUNCTION hello (s CHAR(20))
CREATE INDEX foo_t1 on T1 (id);
CREATE PROCEDURE simpleproc (OUT param1 INT)
CREATE TABLE myCity (a int, b char(20));
create table t1 (id integer);
create table t1(id integer);
CREATE TABLE T1 (id integer);
CREATE TABLE T1 (id integer);
CREATE TABLE T2 (id integer);
CREATE TEMPORARY TABLE T1 (id integer);
DELETE FROM myCity;
DELETE FROM myCity;
DELIMITER ;//
DELIMITER //;
DO
DROP DATABASE FOO;
DROP DATABASE If EXISTS FOO;
DROP EVENT IF EXISTS myevent;
DROP EVENT myevent;
DROP FUNCTION hello;
DROP FUNCTION IF EXISTS hello;
DROP PROCEDURE IF EXISTS simpleproc;
DROP PROCEDURE simpleproc;
DROP TABLE IF EXISTS myCity;
drop table if exists t1;
DROP TABLE IF EXISTS T1;
DROP TABLE IF EXISTS T2;
DROP TABLE myCity;
drop table t1;
DROP TABLE T1;
DROP TABLE T2;
END //
INSERT INTO myCity VALUES (1, 'Milan');
INSERT INTO myCity VALUES (2, 'London');
insert into t1 values(1);
insert into t1 values(1);
INSERT INTO T2 VALUES (@@server_id);
ON SCHEDULE AT CURRENT_TIMESTAMP + INTERVAL 1 HOUR
RETURN CONCAT('Hello, ',s,'!');
RETURNS CHAR(50) DETERMINISTIC
SELECT @a;
SELECT @a;
SELECT COUNT(*) FROM myCity;
select count(*) from t1;
select count(*) from t1;
select count(*) from user where user='maxuser';
    SELECT COUNT(*) INTO param1 FROM t;
SELECT IF(@a <> @TMASTER_ID,'OK (slave)','FAIL (master)') AS result;
SELECT IF(id <> @TMASTER_ID,'OK (slave)','FAIL (master)') AS result FROM T2;
SELECT IF(@@server_id <> @TMASTER_ID,'OK (slave)','FAIL (master)') AS result;
SELECT (@@server_id) INTO @a;
set autocommit=0;
SET autocommit=0;
SET autocommit = 0;
set autocommit=1;
SET autocommit=1;
SET AUTOCOMMIT=1;
set autocommit=OFF;
SET autocommit = oFf;
SET autocommit = Off;
SET AUTOCOMMIT=oN;
START TRANSACTION;
UPDATE t1 SET id = id + 1;
use mysql;
use test;
USE test;
USE test;
