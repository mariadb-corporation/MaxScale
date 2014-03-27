USE test;
DROP TABLE IF EXISTS T1;
SET autocommit=0;
BEGIN;
CREATE TEMPORARY TABLE T1 (id integer); -- NO implicit commit
SELECT (@@server_id) INTO @a;
SELECT @a; --should read from master
DROP TABLE IF EXISTS T1;
COMMIT;
