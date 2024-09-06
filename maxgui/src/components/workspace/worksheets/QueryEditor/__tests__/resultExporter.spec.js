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
import { CsvExporter, JsonExporter, SqlExporter } from '@wkeComps/QueryEditor/resultExporter'
import { describe } from 'vitest'

const dataStub = {
  fields: [
    { value: 'id', index: 0 },
    { value: 'name', index: 1 },
    { value: 'description', index: 2 },
  ],
  data: [
    [1, 'maxscale', null],
    [2, 'mariadb', 'string'],
  ],
}
const metadataStub = [
  { name: 'id', type: 'INT' },
  { name: 'name', type: 'VARCHAR', length: 255 },
  { name: 'description', type: 'TEXT' },
]

describe(`resultExporter`, () => {
  describe('JsonExporter', () => {
    it('Should generate correct JSON string', () => {
      const str = new JsonExporter(dataStub).export()
      expect(str).toBe(
        '[{"id":1,"name":"maxscale","description":null},{"id":2,"name":"mariadb","description":"string"}]'
      )
    })
  })

  describe('CsvExporter', () => {
    function getLines({ str, lineTerminator }) {
      return str.split(lineTerminator).slice(0, -1) // ignore the last empty line
    }

    it.each`
      fieldsTerminatedBy
      ${'\t'}
      ${','}
      ${';'}
    `('Should use "$fieldsTerminatedBy" to separate fields', async ({ fieldsTerminatedBy }) => {
      const csvStr = new CsvExporter({
        ...dataStub,
        opts: {
          fieldsTerminatedBy,
          linesTerminatedBy: '\n',
          nullReplacedBy: 'NULL',
          withHeaders: false,
        },
      }).export()

      const rows = getLines({ str: csvStr, lineTerminator: '\n' })
      rows.forEach((row) =>
        expect(row.split(fieldsTerminatedBy).length).toBe(dataStub.fields.length)
      )
    })

    it.each`
      linesTerminatedBy
      ${'\n'}
      ${'\r\n'}
    `('Should use "$linesTerminatedBy" to terminate lines', async ({ linesTerminatedBy }) => {
      const csvStr = new CsvExporter({
        ...dataStub,
        opts: {
          fieldsTerminatedBy: '\t',
          linesTerminatedBy,
          nullReplacedBy: 'NULL',
          withHeaders: false,
        },
      }).export()
      const lines = getLines({ str: csvStr, lineTerminator: linesTerminatedBy })
      expect(lines.length).toBe(dataStub.data.length)
    })

    it.each`
      nullReplacedBy
      ${'\\N'}
      ${'NULL'}
    `('Should replace null values with "$nullReplacedBy', async ({ nullReplacedBy }) => {
      const csvStr = new CsvExporter({
        ...dataStub,
        opts: {
          fieldsTerminatedBy: '\t',
          linesTerminatedBy: '\n',
          nullReplacedBy,
          withHeaders: false,
        },
      }).export()
      const lines = getLines({ str: csvStr, lineTerminator: '\n' })
      lines.forEach((line, i) =>
        // From dataStub, only line 1 has NULL value
        expect(line.includes(nullReplacedBy)).toBe(i === 0)
      )
    })

    it.each`
      case                     | withHeaders
      ${'include headers'}     | ${true}
      ${'not include headers'} | ${false}
    `('Should $case when withHeaders is $withHeaders', ({ withHeaders }) => {
      const csvStr = new CsvExporter({
        ...dataStub,
        opts: {
          fieldsTerminatedBy: '\t',
          linesTerminatedBy: '\n',
          nullReplacedBy: 'NULL',
          withHeaders,
        },
      }).export()

      const lines = getLines({ str: csvStr, lineTerminator: '\n' })
      const headerLine = lines[0]
      const expectedHeaderLine = dataStub.fields.map((item) => item.value).join('\t')
      if (withHeaders) expect(headerLine).toBe(expectedHeaderLine)
      else expect(headerLine).not.toBe(expectedHeaderLine)
    })
  })

  describe('SqlExporter', () => {
    it.each`
      opt                       | contains
      ${'bothStructureAndData'} | ${['CREATE TABLE', 'INSERT INTO']}
      ${'structure'}            | ${['CREATE TABLE']}
      ${'data'}                 | ${['INSERT INTO']}
    `('Should generate correct SQL script when chosenSqlOpt is $opt', async ({ opt, contains }) => {
      const sqlScript = new SqlExporter({ ...dataStub, metadata: metadataStub, opt }).export()
      contains.forEach((part) => {
        expect(sqlScript).toContain(part)
      })
    })
  })
})
