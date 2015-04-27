# Query Log All Filter

## Overview

The  Query Log All (QLA)  filter  is  a  filter  module  for  MaxScale  that  is  able  to  log  all  query  content  on  a  per  client session  basis.  Logs  are  written  in  a  csv  format  file  that  lists  the  time  submitted  and  the  SQL statement  text.

## Configuration

The  configuration  block  for  the  QLA  filter  requires  the  minimal  filter  options  in  it's  section  within  the  MaxScale.cnf  file,  stored  in  /etc/MaxScale.cnf.
```
[MyLogFilter]
type=filter
module=qlafilter

[MyService]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=MyLogFilter
```

## Filter Options

The  QLA  filter  accepts  one  option  value,  this  is  the  name  that  is  used  for  the  log  files  that  are written.  The  file  that  is  created  appends  the  session  number  to  the  name  given  in  the  options entry.  For example:

```
options=/tmp/QueryLog
```

would  create  log  files  /tmp/QueryLog.1  etc.

Note,  this  is  included  for  backward  compatibility  with  the  version  of  the  QLA  filter  that  was provided  in  the  initial  filters  implementation  preview  in  0.7  of  MaxScale.  The  filebase  parameter  can  now  be  used  and  will  take  precedence  over  the  filter  option.

## Filter Parameters

The  QLA  filter  accepts  a  number  of  optional  parameters,  these  were  introduced  in  the  1.0  release  of  MaxScale.

### Filebase

The  basename  of  the  output  file  created  for  each  session.  A  session  index  is  added  to  the  filename  for  each  file  written.

```
filebase=/tmp/SqlQueryLog
```

The  filebase  may  also  be  set  as  the  filter,  the  mechanism  to  set  the  filebase  via  the  filter  option  is  superseded  by  the  parameter.  If  both  are  set  the  parameter  setting  will  be  used  and  the  filter  option  ignored.

### Match

An  optional  parameter  that  can  be  used  to  limit  the  queries  that  will  be  logged  by  the  QLA  filter.  The  parameter  value  is  a  regular  expression  that  is  used  to  match  against  the  SQL  text.  Only  SQL  statements  that  matches  the  text  passed  as  the  value  of  this  parameter  will  be  logged.

```
match=select.*from.*customer.*where
```

All  regular  expressions  are  evaluated  with  the  option  to  ignore  the  case  of  the  text,  therefore  a  match  option  of  select  will  match  both  select,  SELECT  and  any  form  of  the  word  with  upper  or  lowercase  characters.

### Exclude

An  optional  parameter  that  can  be  used  to  limit  the  queries  that  will  be  logged  by  the  QLA  filter.  The  parameter  value  is  a  regular  expression  that  is  used  to  match  against  the  SQL  text.  SQL  statements  that  match  the  text  passed  as  the  value  of  this  parameter  will  be  excluded  from  the  log  output.

```
exclude=where
```

All  regular  expressions  are  evaluated  with  the  option  to  ignore  the  case  of  the  text,  therefore  an  exclude  option  of  select  will  exclude  statements  that  contain  both  select,  SELECT  or  any  form  of  the  word  with  upper  or  lowercase  characters.

### Source

The  optional  source  parameter  defines  an  address  that  is  used  to  match  against  the  address  from  which  the  client  connection  to  MaxScale  originates.  Only  sessions  that  originate  from  this  address  will  be  logged.

```
source=127.0.0.1
```

### User

The  optional  user  parameter  defines  a  user  name  that  is  used  to  match  against  the  user  from  which  the  client  connection  to  MaxScale  originates.  Only  sessions  that  are  connected  using  this  username  are  logged.

```
user=john
```

## Examples

### Example 1 - Query without primary key

Imagine  you  have  observed  an  issue  with  a  particular  table  and  you  want  to  determine  if  there  are  queries  that  are  accessing  that  table  but  not  using  the  primary  key  of  the  table.  Let's  assume  the  table  name  is  PRODUCTS  and  the  primary  key  is  called  PRODUCT_ID.    Add  a  filter  with  the  following  definition:

```
[ProductsSelectLogger]
type=filter
module=qlafilter
match=SELECT.*from.*PRODUCTS  .*
exclude=WHERE.*PRODUCT_ID.*
filebase=/var/logs/qla/SelectProducts

[Product Service]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=ProductsSelectLogger
```

The  result  of  then  putting  this  filter  into  the  service  used  by  the  application  would  be  a  log  file  of  all  select  queries  that  mentioned  the  table  but  did  not  mention  the  PRODUCT_ID  primary  key  in  the  predicates  for  the  query.
