SET autocommit = 1; 
SELECT @@server_id INTO @a;
START TRANSACTION; 
SELECT @@server_id INTO @b;
COMMIT;
SELECT (@a-@b) INTO @c;
SELECT @a, @b, @c;