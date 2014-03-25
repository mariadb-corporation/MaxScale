USE test; 
SET autocommit = 0; 
SET @a= -1; 
SET @b = -2; 
START TRANSACTION; 
CREATE TABLE IF NOT EXISTS myCity (a int, b char(20)); 
INSERT INTO myCity VALUES (1, 'Milan'); 
INSERT INTO myCity VALUES (2, 'London'); 
COMMIT;
-- Now there are two values
START TRANSACTION; 
DELETE FROM myCity; 
-- Now there are zero values
SET @a = (SELECT COUNT(1) FROM myCity); -- implicit COMMIT for DELETE operation
ROLLBACK; -- nothing to roll back 
START TRANSACTION; 
SET @b = (SELECT COUNT(*) FROM myCity); -- implicit COMMIT for empty trx
START TRANSACTION; 
DROP TABLE myCity; 
SELECT (@a+@b) AS res; -- counts 0+0 ans sets the result to 'res'
COMMIT;
