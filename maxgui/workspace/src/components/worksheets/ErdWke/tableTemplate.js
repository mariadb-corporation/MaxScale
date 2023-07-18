/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
// The template must be in a similar format as the result of SHOW CREATE TABLE
export default name => `CREATE TABLE \`${name}\` (
    \`Id\` INT NOT NULL,
    PRIMARY KEY (\`Id\`)
  ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4`

export const employees = () => `CREATE TABLE \`employees\` (
    \`id\` int(11) NOT NULL AUTO_INCREMENT,
    \`first_name\` varchar(50) NOT NULL,
    \`last_name\` varchar(50) NOT NULL,
    \`department_id\` int(11) DEFAULT NULL,
    PRIMARY KEY (\`id\`)
  ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4`

export const departments = () => `CREATE TABLE \`departments\` (
    \`id\` int(11) NOT NULL AUTO_INCREMENT,
    \`name\` varchar(100) NOT NULL,
    PRIMARY KEY (\`id\`)
  ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4`

export const projects = () => `CREATE TABLE \`projects\` (
    \`id\` int(11) NOT NULL AUTO_INCREMENT,
    \`name\` varchar(100) NOT NULL,
    \`department_id\` int(11) DEFAULT NULL,
    PRIMARY KEY (\`id\`)
  ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4`
