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
	feedback_url=xxxxxx
	feedback_user_info=yyyyy


## Manual Operation

If it’s not possible to send data due to firewall or security settings the report could be generated manually (feedback_user_info is required) via MaxAdmin


MaxScale>show feedback report


Report could be saved to report.txt file:


maxadmin -uxxx -pyyy show feedbackreport > ./report.txt

curl -F data=@./report.txt https://mariadb.org/feedback_plugin/post

