# Securing Your MaxScale Deployment

There are five main components that you need to make sure are completed
before you go into production:

* Encrypting Plaintext Passwords
* Securing the GUI Interface
* Managing Users & Passwords
* Enabling Audit Logging
* Encrypting Database Connections

## Encrypting Plaintext Passwords
Ensuring the security of your MaxScale setup involves stringent control over
the key file permissions. Utilizing
[maxkeys](../Getting-Started/Configuration-Guide.md#encrypting-passwords)
is an effective approach to generate a secure key file.

This generates a keyfile in `/var/lib/maxscale`
```
$ maxkeys
```
See
[Encrypting Passwords](../Getting-Started/Configuration-Guide.md#encrypting-passwords)
for more information about `maxkeys`.

Once generated, this key file can be relocated to a secure location. This
key file serves a dual purpose: it enables the encryption of passwords and
facilitates MaxScale in decrypting those encrypted passwords.

To maintain confidentiality, it is crucial to adjust the ownership and
permissions of the key file appropriately using `chown`. This step ensures
that the key file remains secure and inaccessible to unauthorized users.
```
$ chown maxscale:maxscale /var/lib/maxscale/.secrets
```
Following the secure setup of the key file, you can proceed to encrypt the
plaintext passwords of users already created in your databases.
```
$ maxpasswd plaintextpassword
96F99AA1315BDC3604B006F427DD9484
```
These encrypted passwords can then replace the plaintext passwords in your
MaxScale configuration (CNF) files. This enhances the overall security
of your database system by reducing the risk that passwords are accidentally
shared.
```
[MariaDB-Service]
type=service
router=readwritesplit
servers=MariaDB1,MariaDB2,MariaDB3
user=maxscale-user
password=96F99AA1315BDC3604B006F427DD9484
```

## Securing the GUI Interface

To enhance the security of your MaxScale environment, it’s crucial to
configure the GUI host address properly. The default setting, `0.0.0.0`,
allows unrestricted access from any network, which poses a significant
security risk. Instead, you should set the `admin_host` to a more secure
address. Additionally, you can change the default port (8989) to another
port for added security. For example, you can restrict access to the
localhost by setting:
```
[maxscale]
admin_host=127.0.0.1
admin_port=2222
```
Alternatively, you can specify an internal network IP address to limit
access within your internal network, such as:
```
[maxscale]
admin_host=10.0.0.3
admin_port=2222
```
If you need to allow external access, ensure that the network is adequately
secured and that only authorized users can access the MaxScale
interface. Consult with your network administrator to determine the most
appropriate and secure configuration.

### Enabling TLS Encryption

To further secure your MaxScale setup, enable TLS encryption for data in
transit. Follow these steps to configure SSL:

#### 1. Set Up SSL Keys and Certificates:

* Generate SSL keys and certificates. See
  [Certificate Creation with OpenSSL](https://mariadb.com/kb/en/certificate-creation-with-openssl/)

* Add them to the MaxScale configuration file.

#### 2. Update the MaxScale Configuration:

* Enable secure connections by setting `admin_secure_gui` to `true`.

* Specify the paths to the SSL certificate and key files in your CNF file:
```
[maxscale]
admin_secure_gui=true
admin_ssl_key=/certs/maxscale-key.pem
admin_ssl_cert=/certs/maxscale-cert.pem
admin_ssl_ca_cert=/certs/ca-cert.pem
```

#### 3. Verify Encryption:

* Use the `Maxctrl` command to verify that TLS encryption is functioning correctly:
```
$ maxctrl --user=my_user --password=my_password --secure --tls-ca-cert=/certs/ca-cert.pem --tls-verify-server-cert=false show maxscale
```

#### 4. Update Default Credentials:

* It’s essential to change the default admin passwords. Create a new user with a strong password and remove the default admin user for enhanced security.

## Managing Users & Passwords

MaxScale allows you to manage user access to its GUI, offering different
permission levels to suit various operational needs. Currently, MaxScale
supports two primary roles: `admin` and `basic`. This functionality is
particularly useful for organizations with hierarchical structures or
distinct departments, enabling you to grant status view access without
allowing execution or manipulation capabilities.

### Creating and Deleting Users

To create or delete users in the MaxScale GUI, you can use the `maxctrl`
command. Here’s an example of creating a user with administrative
privileges:
```
$ maxctrl create user my_user my_password --type=admin
```
To remove an existing user, such as the default admin user, you can use the following command:
```
$ maxctrl destroy user admin
```

### Managing Users in the GUI

The MaxScale GUI also provides functionality to manage user access and
update the admin password. Through the GUI, you can:

* Add Users: Create users with basic or admin access.
* Modify User Permissions: Change roles as needed to adapt to evolving security requirements.
* Update Admin Password: Enhance security by regularly updating the admin password.

By leveraging these features, you can ensure that your MaxScale environment
remains secure and that user access is appropriately managed according to
your organization’s needs.

## Enabling Audit Logging

Turn on admin auditing to log all login, connection, and configuration
changes. Choose an audit file location and set up log rotation.

### Enabling and Managing Admin Auditing in MaxScale

Admin auditing in MaxScale provides comprehensive tracking of all
administrative activities, including logins, connections, and
modifications. These activities are recorded in an audit file for enhanced
security and traceability.

To enable admin auditing, add the following configuration to your MaxScale
configuration file:
```
[maxscale]
admin_audit = true
admin_audit_file = /var/log/maxscale/audit_files/audit.csv
```
This configuration activates auditing and specifies the location of the
audit file. Ensure that the specified directory exists before restarting
MaxScale.

### Setting Up and Managing Audit Logs

#### 1. Enable Auditing:

* Add the configuration lines to your MaxScale configuration file.

* Verify the directory specified in `admin_audit_file` exists.

#### 2. Audit File Management:

* Implement log rotation to manage the size and number of audit files. This
  can be achieved using standard Linux log rotation tools. See
  [Rotating Logs on Unix and Linux](https://mariadb.com/kb/en/rotating-logs-on-unix-and-linux/)

For manual log rotation, you can use the following MaxCtrl command:
```
$ maxctrl rotate logs
```

## Encrypting Database Connections
Configuring SSL Encryption for MaxScale with an Encrypted MariaDB Server

If you have already implemented encryption on your MariaDB server, it’s
crucial to extend this encryption configuration to MaxScale to ensure secure
communication. Once encryption is enabled on your MariaDB server, follow
these steps to configure MaxScale to utilize SSL.

Steps to Configure SSL in MaxScale:

### 1. Enable SSL:

* Add `ssl=true` to each server section in your MaxScale configuration file.

### 2. Verify Peer Certificates:

* Add `ssl_verify_peer_certificate=true` to ensure that MaxScale verifies
  the server’s SSL certificates, providing an additional layer of security.

Your MaxScale configuration file should look something like this:
```
[MariaDB-Server1]
type=server
ssl=true
ssl_verify_peer_certificate=true
```

These settings instruct MaxScale to use SSL for connections to the MariaDB
server and to verify peer certificates, enhancing the security of data in
transit.
