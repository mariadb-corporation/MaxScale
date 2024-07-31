/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const DEFINER_DOC = `#[DEFINER = {user | CURRENT_USER | role | CURRENT_ROLE}]`
const PARAMS_DOC = `
  /*Parameters:
  * [ IN | OUT | INOUT ] param_name type, [,...]
  * e.g.: IN param1 INT, OUT param2 VARCHAR(100), INOUT param3 INT
  */
`
const CHARACTERISTIC_DOC = `
/* LANGUAGE SQL
|  [NOT] DETERMINISTIC
|  { CONTAINS SQL | NO SQL | READS SQL DATA | MODIFIES SQL DATA }
|  SQL SECURITY { DEFINER | INVOKER }
|  COMMENT 'string'
*/`

// The template must be in a similar format as the result of SHOW CREATE TABLE
function createTbl(name) {
  return `CREATE TABLE \`${name}\` (
    \`id\` INT(11) NOT NULL,
    PRIMARY KEY (\`id\`)
  ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4`
}

function createView(schema) {
  return `CREATE #[OR REPLACE]
#[ALGORITHM = {UNDEFINED | MERGE | TEMPTABLE}]
${DEFINER_DOC}
#[SQL SECURITY {DEFINER | INVOKER}]
VIEW /*[IF NOT EXISTS]*/ \`${schema}\`.\`view_name\` #[(column_list)]
AS 
# Write your SELECT statement below
(
  SELECT
    column1,
    column2
  FROM
    table_name
)
#[WITH [CASCADED | LOCAL] CHECK OPTION]
`
}

function createSP(schema) {
  return `CREATE #[OR REPLACE]
${DEFINER_DOC}
PROCEDURE /*[IF NOT EXISTS]*/ \`${schema}\`.\`sp_name\` (${PARAMS_DOC}) ${CHARACTERISTIC_DOC}
BEGIN
# Write your routine_body here
END`
}

function createFN(schema) {
  return `CREATE #[OR REPLACE]
${DEFINER_DOC}
#[AGGREGATE]
FUNCTION /*[IF NOT EXISTS]*/ \`${schema}\`.\`func_name\` (${PARAMS_DOC}) RETURNS #type ${CHARACTERISTIC_DOC}
BEGIN
# Write your function body here
END`
}

function createTrigger({ schema, tbl }) {
  return `CREATE #[OR REPLACE]
${DEFINER_DOC}
TRIGGER /*[IF NOT EXISTS]*/ \`${schema}\`.\`trigger_name\` #BEFORE | AFTER
#INSERT | UPDATE | DELETE
ON \`${tbl}\` FOR EACH ROW
#[{ FOLLOWS | PRECEDES } other_trigger_name ]
BEGIN
# Write your trigger statements here
END`
}

export default {
  createTbl,
  createView,
  createSP,
  createFN,
  createTrigger,
}
