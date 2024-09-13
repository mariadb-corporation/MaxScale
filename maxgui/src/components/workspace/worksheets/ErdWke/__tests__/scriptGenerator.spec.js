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
import scriptGenerator, {
  genCreateSchemas,
  genCreateTables,
} from '@wkeComps/ErdWke/scriptGenerator'
import { formatSQL } from '@/utils/queryUtils'
import TableScriptBuilder from '@/utils/TableScriptBuilder.js'
import { editorDataStub, tableColNameMapStub } from '@wsComps/TblStructureEditor/__tests__/stubData'

describe(`scriptGenerator`, () => {
  vi.mock('@/utils/TableScriptBuilder.js', () => ({
    default: vi.fn(() => ({
      build: vi.fn(() => 'CREATE TABLE...'),
      buildNewFkSQL: vi.fn(() => 'ALTER TABLE t1 ADD CONSTRAINT...'),
    })),
  }))

  vi.mock('@/utils/queryUtils', () => ({ formatSQL: vi.fn((sql) => sql) }))

  const schemaStub = ['test']
  const tablesStub = [editorDataStub]
  const refTargetMapStub = { [editorDataStub.id]: editorDataStub }

  it('Should generate SQL script with schemas and tables', () => {
    const { name, time, sql } = scriptGenerator({
      schemas: schemaStub,
      tables: tablesStub,
      refTargetMap: refTargetMapStub,
      tablesColNameMap: tableColNameMapStub,
    })
    expect(name).toBeTypeOf('string')
    expect(time).toBeTypeOf('string')
    expect(sql).toBeTypeOf('string')
    expect(formatSQL).toHaveBeenCalled()
  })

  it('Should generate SQL for creating schemas when genCreateSchemas is called', () => {
    const schemas = ['schema1', 'schema2']

    const result = genCreateSchemas(schemas)

    expect(result[0]).toContain('# Create schemas')
    expect(result[1]).toBe('CREATE SCHEMA IF NOT EXISTS `schema1`;')
    expect(result[2]).toBe('CREATE SCHEMA IF NOT EXISTS `schema2`;')
  })

  it('should generate SQL for creating tables and foreign key constraints', () => {
    const result = genCreateTables({
      tables: tablesStub,
      refTargetMap: refTargetMapStub,
      tablesColNameMap: tableColNameMapStub,
    })

    expect(result[0]).toContain('# Create tables')
    expect(result[1]).toBe('CREATE TABLE...')
    expect(result[2]).toContain('# Add new tables constraints')
    expect(result[3]).toBe('ALTER TABLE t1 ADD CONSTRAINT...')

    expect(TableScriptBuilder).toHaveBeenCalledWith({
      initialData: {},
      stagingData: tablesStub[0],
      refTargetMap: refTargetMapStub,
      tablesColNameMap: tableColNameMapStub,
      options: { isCreating: true, skipSchemaCreation: true, skipFkCreation: true },
    })
  })
})
