-- simple read with variable from master
BEGIN;
SELECT (@@server_id) INTO @a;
SELECT @a;
COMMIT;
