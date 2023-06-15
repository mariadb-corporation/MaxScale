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
