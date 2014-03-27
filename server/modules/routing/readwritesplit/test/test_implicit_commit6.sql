USE test;
DROP FUNCTION IF EXISTS hello;
SET autocommit=0;
BEGIN;
CREATE FUNCTION hello (s CHAR(20))
RETURNS CHAR(50) DETERMINISTIC
RETURN CONCAT('Hello, ',s,'!'); -- implicit COMMIT
SELECT (@@server_id) INTO @a;
SELECT @a; --should read from slave
DROP FUNCTION IF EXISTS hello;
COMMIT;
