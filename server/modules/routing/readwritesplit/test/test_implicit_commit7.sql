USE test;
DROP TABLE IF EXISTS T1;
CREATE TABLE T1 (id integer); -- implicit commit
SET autocommit=1;
BEGIN;
CREATE INDEX foo_t1 on T1 (id); -- implicit commit
SELECT (@@server_id) INTO @a;
SELECT @a; --should read from slave
DROP TABLE IF EXISTS T1;
COMMIT;
