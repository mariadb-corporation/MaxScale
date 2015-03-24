# MaxScale Notification Service and Feedback Support

Massimiliano Pinto

Last Updated: 10th March 2015

## Contents

## Document History

<table>
  <tr>
    <td>Date</td>
    <td>Change</td>
    <td>Who</td>
  </tr>
  <tr>
    <td>10th March 2015</td>
    <td>Initial version</td>
    <td>Massimiliano Pinto</td>
  </tr>
</table>


## Overview

The purpose of Notification Service in MaxScale is for a customer registered for the service to receive update notices, security bulletins, fixes and workarounds that are tailored to the database server configuration.

## MaxScale Setup

MaxScale may collect the installed plugins and send the informations nightly, between 2:00 AM and 4:59 AM.

It tries to send data and if there is any failure (timeout, server is down, etc), the next retry is in 1800 seconds (30 minutes)

This feature is not enabled by default: MaxScale must be configured in [feedback] section:


	[feedback]
	feedback_enable=1
	feedback_url=https://enterprise.mariadb.com/feedback/post
	feedback_user_info=x-y-z-w

The activation code that will be provided by MariaDB corp upon request by the customer and it shlud be put in feedback_user_info.

Example:
feedback_user_info=0467009f-b04d-45b1-a77b-b6b2ec9c6cf4


MaxScale generates the feedback report containing following information:

 -The activation code used to enable feedback 
 - MaxScale Version
 - An identifier of the MaxScale installation, i.e. the HEX encoding of SHA1 digest of the first network interface MAC address
 - Operating System (i.e Linux)
 - Operating Suystem Distribution (i.e. CentOS release 6.5 (Final))
 - All the modules in use in MaxScale and their API and version
 - MaxScale server UNIX_TIME at generation time

MaxScale shall send the generated feedback report to a feedback server specified in feedback_url


## Manual Operation

If it’s not possible to send data due to firewall or security settings the report could be generated manually (feedback_user_info is required) via MaxAdmin


MaxScale>show feedbackreport


Report could be saved to report.txt file:


maxadmin -uxxx -pyyy show feedbackreport > ./report.txt

curl -F data=@./report.txt https://mariadb.org/feedback_plugin/post


Report Example:

	FEEDBACK_SERVER_UID     6B5C44AEA73137D049B02E6D1C7629EF431A350F
	FEEDBACK_USER_INFO      0467009f-b04d-45b1-a77b-b6b2ec9c6cf4
	VERSION 1.0.6-unstable
	NOW     1425914890
	PRODUCT maxscale
	Uname_sysname   Linux
	Uname_distribution      CentOS release 6.5 (Final)
	module_maxscaled_type   Protocol
	module_maxscaled_version        V1.0.0
	module_maxscaled_api    1.0.0
	module_maxscaled_releasestatus  GA
	module_telnetd_type     Protocol
	module_telnetd_version  V1.0.1
	module_telnetd_api      1.0.0
	module_telnetd_releasestatus    GA
