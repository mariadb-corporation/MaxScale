select sleep(2);
select * from tst where lname='Doe';
select 1,2,3,4,5,6 from tst;
select * from tst where fname like '%a%';
select * from tst where lname like '%e%' order by fname;
insert into tst values ("John"," Doe"),("Donald","Duck"),("Plato",""),("Richard","Stallman");
insert into tst values ("Jane"," Doe"),("Daisy","Duck"),("Marie","Curie");
insert into tst values ("John","Doe"),("Donald","Duck"),("Plato",""),("Richard","Stallman");
insert into tst values ("Jane","Doe"),("Daisy","Duck"),("Marie","Curie");
insert into tst values ("John","Doe"),("Donald","Duck"),("Plato",""),("Richard","Stallman");
insert into tst values ("Jane","Doe"),("Daisy","Duck"),("Marie","Curie");
update tst set fname="Farmer", lname="McDonald" where lname="%Doe" and fname="John";
update tst set fname="John" where lname="Doe";
update tst set lname="Philosopher" where fname="Plato";
update tst set fname="Human" where fname like 'Richard%';
update tst set lname="Creature" where lname like '%man%';
update tst set fname="Jane" where lname="%Doe";
update tst set lname="Human" where fname like '%a%' or lname like '%a%';

