USE test;
DROP PROCEDURE IF EXISTS simpleproc;
SET autocommit=1;
BEGIN;
DELIMITER //
CREATE PROCEDURE simpleproc (OUT param1 INT)
BEGIN
    SELECT COUNT(*) INTO param1 FROM t;
END //
DELIMITER ;
SELECT (@@server_id) INTO @a;
SELECT @a; --should read from slave
DROP PROCEDURE IF EXISTS simpleproc;
COMMIT;
