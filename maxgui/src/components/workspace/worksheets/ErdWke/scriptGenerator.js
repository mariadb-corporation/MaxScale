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
import { quotingIdentifier } from '@/utils/helpers'
import { formatSQL } from '@/utils/queryUtils'
import TableScriptBuilder from '@/utils/TableScriptBuilder.js'
import sqlCommenter from '@/utils/sqlCommenter.js'

export function genCreateSchemas(schemas) {
  const parts = []
  schemas.forEach((s, i) => {
    if (i === 0) parts.push(sqlCommenter.genSection('Create schemas'))
    const schema = quotingIdentifier(s)
    parts.push(`CREATE SCHEMA IF NOT EXISTS ${schema};`)
  })
  return parts
}

export function genCreateTables({ tables, refTargetMap, tablesColNameMap }) {
  const parts = [],
    tablesFks = []
  // new tables
  tables.forEach((tbl, i) => {
    if (i === 0) parts.push(sqlCommenter.genSection('Create tables'))
    const builder = new TableScriptBuilder({
      initialData: {},
      stagingData: tbl,
      refTargetMap,
      tablesColNameMap,
      options: { isCreating: true, skipSchemaCreation: true, skipFkCreation: true },
    })
    parts.push(builder.build())
    const fks = builder.buildNewFkSQL()
    if (fks) tablesFks.push(fks)
  })

  if (tablesFks.length) {
    parts.push(sqlCommenter.genSection('Add new tables constraints'))
    parts.push(tablesFks.join(''))
  }
  return parts
}

/**
 * Generates SQL scripts for creating schemas, tables, and foreign key constraints.
 * @param {object} param
 * @param {Array<string>} param.schemas - List of schema names to be created.
 * @param {Array<object>} param.tables - List of table objects containing the table definitions.
 * @param {object} param.refTargetMap - Mapping of reference targets for foreign keys.
 * @param {object} param.tablesColNameMap - Mapping of table column names.
 * @returns {{name: string, time: string, sql: string}} - An object containing the SQL script details.
 */
export default ({ schemas, tables, refTargetMap, tablesColNameMap }) => {
  const parts = []
  parts.push(...genCreateSchemas(schemas))
  parts.push(...genCreateTables({ tables, refTargetMap, tablesColNameMap }))

  const { name, time, content } = sqlCommenter.genHeader()

  let sql = formatSQL(parts.join('\n'))
  sql = `${content}\n\n${sql}`
  return { name, time, sql }
}
