#create user repl@'%' identified by 'repl'; 
grant replication slave on *.* to repl@'%' identified by 'repl'; 

FLUSH PRIVILEGES;
