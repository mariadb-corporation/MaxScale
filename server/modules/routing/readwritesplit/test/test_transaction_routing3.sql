-- Read from slave after implicit COMMIT
USE test; 
START TRANSACTION; 
CREATE TABLE IF NOT EXISTS T2 (id integer); 
INSERT INTO T2 VALUES (@@server_id);
SET AUTOCOMMIT=1;
SELECT id from T2; -- read transaction's modifications from slave
