SET autocommit = 0; 
SELECT @@in_transaction INTO @a;
SELECT @a;