select 1;-- comment after statement
select 1;# comment after statement
select /* inline comment */ 1;
select /*! 1 + */ 1;
select /*!300000 1 + */ 1;
select /*!300000 1 + */ 1;
SELECT 2 /* +1 */;
SELECT 1 /*! +1 */;
SELECT 1 /*!50101 +1 */;
SELECT 2 /*M! +1 */;
SELECT 2 /*M!50101 +1 */;
SELECT 2 /* 
SELECT 2 /*
SELECT 2 /*/
SELECT 2 /**/
