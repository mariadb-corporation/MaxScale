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
import { t as typy } from 'typy'
import { escapeBackslashes, lodash, quotingIdentifier } from '@/utils/helpers'
import { formatSQL } from '@/utils/queryUtils'
import sqlCommenter from '@/utils/sqlCommenter.js'

// values are used for i18n
export const SQL_EXPORT_OPTS = Object.freeze({
  STRUCTURE: 'structure',
  DATA: 'data',
  BOTH: 'bothStructureAndData',
})

class DataInterface {
  constructor({ fields, data }) {
    this.fields = fields
    this.data = data
    this.fieldIndexes = fields.map((item) => item.index)
  }

  getRowData({ row, escaper }) {
    return row.reduce((acc, value, index) => {
      if (this.fieldIndexes.includes(index)) acc.push(escaper(value))
      return acc
    }, [])
  }
}

export class CsvExporter extends DataInterface {
  constructor({ opts, ...args }) {
    super(args)
    this.opts = opts
  }

  escaper(value, nullReplacedBy) {
    return typy(value).isNull ? nullReplacedBy : escapeBackslashes(value)
  }

  export() {
    const { fieldsTerminatedBy, linesTerminatedBy, nullReplacedBy, withHeaders } = this.opts
    let str = ''

    if (withHeaders) {
      const escapedFields = this.fields.map((item) => this.escaper(item.value, nullReplacedBy))
      str = `${escapedFields.join(fieldsTerminatedBy)}${linesTerminatedBy}`
    }

    str += this.data
      .map((row) =>
        this.getRowData({ row, escaper: (v) => this.escaper(v, nullReplacedBy) }).join(
          fieldsTerminatedBy
        )
      )
      .join(linesTerminatedBy)

    return `${str}${linesTerminatedBy}`
  }
}

export class JsonExporter extends DataInterface {
  export() {
    const arr = this.data.map((row) => {
      const obj = {}
      this.fields.forEach((item) => (obj[item.value] = row[item.index]))
      return obj
    })
    return JSON.stringify(arr)
  }
}

export class SqlExporter extends DataInterface {
  constructor({ metadata, opt, ...args }) {
    super(args)
    this.metadata = metadata
    this.opt = opt
  }

  escaper(v) {
    if (typy(v).isNull) return 'NULL'
    if (typy(v).isString) return `'${v.replace(/'/g, "''")}'`
    return v
  }

  buildColDef(colName) {
    const { type, length } = lodash.keyBy(this.metadata, 'name')[colName]
    const tokens = [quotingIdentifier(colName), type]
    if (length) tokens.push(`(${length})`)
    return tokens.join(' ')
  }

  genTableCreationScript(identifier) {
    const tokens = ['CREATE TABLE', `${identifier}`, '(']
    this.fields.forEach((item, i) => {
      tokens.push(`${this.buildColDef(item.value)}${i < this.fields.length - 1 ? ',' : ''}`)
    })
    tokens.push(');')
    return sqlCommenter.genSection('Create') + '\n' + tokens.join(' ')
  }

  genInsertionScript(identifier) {
    const escapedFields = this.fields.map((item) => quotingIdentifier(item.value)).join(', ')
    const insertionSection = `${sqlCommenter.genSection('Insert')}\n`
    if (!this.data.length) return insertionSection
    return (
      insertionSection +
      `INSERT INTO ${identifier} (${escapedFields}) VALUES` +
      this.data
        .map((row) => `(${this.getRowData({ row, escaper: this.escaper }).join(',')})`)
        .join(',')
    )
  }

  export() {
    const { STRUCTURE, DATA } = SQL_EXPORT_OPTS
    const tblNames = lodash.uniq(this.metadata.map((item) => item.table))
    const identifier = quotingIdentifier(tblNames.join('_'))

    let script = ''

    switch (this.opt) {
      case STRUCTURE:
        script = this.genTableCreationScript(identifier)
        break
      case DATA:
        script = this.genInsertionScript(identifier)
        break
      default:
        script =
          this.genTableCreationScript(identifier) + '\n' + this.genInsertionScript(identifier)
        break
    }

    const { content } = sqlCommenter.genHeader()
    return `${content}\n\n${formatSQL(script)}`
  }
}
