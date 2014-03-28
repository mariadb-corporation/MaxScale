USE test; 
SET autocommit = 0; 
START TRANSACTION; 
CREATE TABLE IF NOT EXISTS myCity (a int, b char(20)); 
INSERT INTO myCity VALUES (1, 'Milan'); 
INSERT INTO myCity VALUES (2, 'London'); 
COMMIT; 
START TRANSACTION; 
DELETE FROM myCity;
SELECT COUNT(*) FROM myCity; -- read transaction's modifications from master
COMMIT;