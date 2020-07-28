set -x
vpc_id=`aws ec2 create-vpc --cidr-block 172.30.0.0/16 | grep "VpcId" | sed 's/\"VpcId\": \"//' | sed 's/\"//' | sed 's/ //g' | sed 's/,//'`
echo $vpc_id > vpc_id
subnet_id1=`aws ec2 create-subnet --cidr-block 172.30.0.0/24 --availability-zone eu-west-1b --vpc-id $vpc_id | grep "SubnetId" | sed 's/\"SubnetId\": \"//' | sed 's/\"//' | sed 's/ //g' | sed 's/,//'`
subnet_id2=`aws ec2 create-subnet --cidr-block 172.30.1.0/24 --availability-zone eu-west-1a --vpc-id $vpc_id | grep "SubnetId" | sed 's/\"SubnetId\": \"//' | sed 's/\"//' | sed 's/ //g' | sed 's/,//'`
aws rds create-db-subnet-group --db-subnet-group-name maxscaleaurora --db-subnet-group-description maxscale --subnet-ids $subnet_id1 $subnet_id2

gw_id=`aws ec2 create-internet-gateway | grep "InternetGatewayId" | sed 's/\"InternetGatewayId\": \"//' | sed 's/\"//' | sed 's/ //g' | sed 's/,//'`
aws ec2 attach-internet-gateway --internet-gateway-id $gw_id --vpc-id $vpc_id

aws ec2 modify-vpc-attribute --enable-dns-support  --vpc-id $vpc_id
aws ec2 modify-vpc-attribute --enable-dns-hostnames --vpc-id $vpc_id

aws ec2 modify-subnet-attribute --map-public-ip-on-launch --subnet-id $subnet_id1
aws ec2 modify-subnet-attribute --map-public-ip-on-launch --subnet-id $subnet_id2

# works only if there is only one route table
routetab_id=`aws ec2 describe-route-tables | grep "RouteTableId" | sed 's/\"RouteTableId\": \"//' | sed 's/\"//' | sed 's/ //g' | grep "," | sed 's/,//'`

aws ec2 create-route --route-table-id $routetab_id --gateway-id $gw_id --destination-cidr-block 0.0.0.0/0


aws rds create-db-cluster --database-name=test --engine=aurora --master-username=skysql --master-user-password=skysqlrds --db-cluster-identifier=auroratest --db-subnet-group-name=maxscaleaurora
aws rds create-db-instance --db-cluster-identifier=auroratest --engine=aurora --db-instance-class=db.t2.medium --publicly-accessible --db-instance-identifier=node000
aws rds create-db-instance --db-cluster-identifier=auroratest --engine=aurora --db-instance-class=db.t2.medium --publicly-accessible --db-instance-identifier=node001
aws rds create-db-instance --db-cluster-identifier=auroratest --engine=aurora --db-instance-class=db.t2.medium --publicly-accessible --db-instance-identifier=node002
aws rds create-db-instance --db-cluster-identifier=auroratest --engine=aurora --db-instance-class=db.t2.medium --publicly-accessible --db-instance-identifier=node003


secgr_id=`aws rds describe-db-instances --db-instance-identifier node000 | grep "VpcSecurityGroupId" | sed 's/\"VpcSecurityGroupId\": \"//' | sed 's/\"//' | sed 's/ //g' | sed 's/,//'`
aws ec2 authorize-security-group-ingress --group-id $secgr_id --protocol tcp --port 3306 --cidr 0.0.0.0/0
