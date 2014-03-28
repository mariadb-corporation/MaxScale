DROP DATABASE If EXISTS FOO;
SET autocommit=0;
BEGIN;
CREATE DATABASE FOO; -- implicit commit
SELECT (@@server_id) INTO @a;
SELECT @a; --should read from slave
DROP DATABASE If EXISTS FOO;
COMMIT;
