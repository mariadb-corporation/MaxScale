USE test;
DROP TABLE IF EXISTS T1;
SET autocommit=1;
BEGIN;
CREATE TABLE T1 (id integer); -- implicit commit
SELECT (@@server_id) INTO @a;
SELECT @a; -- should read from slave
DROP TABLE IF EXISTS T1;
COMMIT;
