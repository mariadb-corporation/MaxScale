select md5("?") =?, sleep(?), rand(?);
select * from my1 where md5("?") =?;
select md5("?") =?;
select * from my1 where md5("?") =?;
select sleep(?);
select * from tst where lname='?';
select ?,?,?,?,?,? from tst;
select * from tst where fname like '?';
select * from tst where lname like '?' order by fname;
insert into tst values ("?","?"),("?",null),("?","?");
drop table if exists tst;
create table tst(fname varchar(?), lname varchar(?));
update tst set lname="?" where fname like '?' or lname like '?';
delete from tst where lname like '?' and fname like '?';
select ? from tst where fname='?' or lname like '?';
select ?,?,?,? from tst where name='?' or name='?' or name='?' or name='?';
select count(?),count(?),count(?),count(?),count (?),count(?) from tst;
begin;
BEGIN
BEGIN;
commit;
COMMIT;
COMMIT;
CREATE DATABASE FOO;
CREATE EVENT myevent
CREATE FUNCTION hello (s CHAR(?))
CREATE INDEX foo_t1 on T1 (id);
CREATE PROCEDURE simpleproc (OUT param1 INT)
CREATE TABLE myCity (a int, b char(?));
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
INSERT INTO myCity VALUES (?, '?');
INSERT INTO myCity VALUES (?, '?');
insert into t1 values(?);
insert into t1 values(?);
INSERT INTO T2 VALUES (@@?);
ON SCHEDULE AT CURRENT_TIMESTAMP + INTERVAL ? HOUR
RETURN CONCAT('?',s,'?');
RETURNS CHAR(?) DETERMINISTIC
SELECT @?;
SELECT @?;
SELECT COUNT(*) FROM myCity;
select count(*) from t1;
select count(*) from t1;
select count(*) from user where user='?';
SELECT COUNT(*) INTO param1 FROM t;
SELECT IF(@? <> @?,'?','?') AS result;
SELECT IF(id <> @?,'?','?') AS result FROM T2;
SELECT IF(@@? <> @?,'?','?') AS result;
SELECT (@@?) INTO @?;
set autocommit=?;
SET autocommit=?;
SET autocommit = ?;
set autocommit=?;
SET autocommit=?;
SET AUTOCOMMIT=?;
set autocommit=OFF;
SET autocommit = oFf;
SET autocommit = Off;
SET AUTOCOMMIT=oN;
START TRANSACTION;
UPDATE t1 SET id = id + ?;
use mysql;
use test;
USE test;
USE test;
