/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import MigrationTblScript from '@wkeComps/DataMigration/MigrationTblScript.vue'
import { lodash } from '@/utils/helpers'
import { ETL_STATUS } from '@/constants/workspace'

const dataStub = [
  {
    create: 'create script',
    insert: 'insert script',
    select: 'select script',
    schema: 'Workspace',
    table: 'AnalyzeEditor',
  },
  {
    create: 'create script',
    insert: 'insert script',
    select: 'select script',
    schema: 'Workspace',
    table: 'DdlEditor',
  },
]

const headersStub = [
  { title: 'SCHEMA', value: 'schema' },
  { title: 'TABLE', value: 'table' },
]
const SqlEditorStub = { name: 'SqlEditorStub', template: `<div><slot></slot></div>` }

const mountFactory = (opts) =>
  mount(
    MigrationTblScript,
    lodash.merge(
      {
        props: { data: dataStub, task: { status: ETL_STATUS.INITIALIZING } },
        global: {
          stubs: { SqlEditor: SqlEditorStub },
        },
      },
      opts
    )
  )

describe('MigrationTblScript', () => {
  let wrapper

  it(`Should pass expected data to VDataTableVirtual`, () => {
    wrapper = mountFactory({ shallow: false })
    const { items, fixedHeader, density, height } = wrapper.findComponent({
      name: 'VDataTableVirtual',
    }).vm.$props
    items.forEach((item) => expect(item).toHaveProperty('id'))
    expect(items.map((item) => lodash.omit(item, ['id']))).toStrictEqual(dataStub)
    expect(fixedHeader).toBe(true)
    expect(density).toBe('comfortable')
    expect(height).toBe(wrapper.vm.tableMaxHeight)
  })

  it(`Should pass $attrs to VDataTableVirtual`, () => {
    wrapper = mountFactory({ shallow: false, attrs: { headers: headersStub } })
    const { headers } = wrapper.findComponent({ name: 'VDataTableVirtual' }).vm.$props
    expect(headers).toStrictEqual(headersStub)
  })

  it(`Should render VDataTableVirtual cell slot`, () => {
    wrapper = mountFactory({
      shallow: false,
      slots: { ['item.schema']: '<div data-test="schema"/>' },
    })
    expect(find(wrapper, 'schema').exists()).toBe(true)
  })

  it(`Should not initially render ScriptEditors`, () => {
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'ScriptEditors' }).exists()).toBe(false)
  })

  it(`Should pass expected data to ScriptEditors`, async () => {
    wrapper = mountFactory({ shallow: false })
    // mock showing ScriptEditors
    wrapper.vm.selectedItems = [wrapper.vm.tableRows[0]]
    await wrapper.vm.$nextTick()
    const { modelValue, hasChanged } = wrapper.findComponent({
      name: 'ScriptEditors',
    }).vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.activeRow)
    expect(hasChanged).toBe(wrapper.vm.hasChanged)
  })

  it(`defDataMap should generate UUID and return a map`, () => {
    wrapper = mountFactory()
    expect(wrapper.vm.defDataMap).toBeTypeOf('object')
    Object.values(wrapper.vm.defDataMap).forEach((item) =>
      assert.containsAllKeys(item, ['create', 'insert', 'select', 'schema', 'table', 'id'])
    )
  })

  it(`stagingData should return object without 'id' field`, () => {
    wrapper = mountFactory()
    wrapper.vm.stagingData.forEach((obj) => expect(obj).not.toHaveProperty('id'))
  })

  it(`Should immediately emit get-staging-data event`, () => {
    wrapper = mountFactory()
    expect(wrapper.emitted('get-staging-data')).not.toHaveLength(0)
  })

  it(`defDataMap handler should immediately set tableRows`, () => {
    wrapper = mountFactory()
    expect(wrapper.vm.tableRows).toStrictEqual(Object.values(wrapper.vm.defDataMap))
  })

  it(`Should immediately select the first row as active`, () => {
    wrapper = mountFactory()
    expect(wrapper.vm.selectedItems).toStrictEqual([wrapper.vm.tableRows[0]])
  })

  it(`Should immediately emit get-activeRow event`, () => {
    wrapper = mountFactory()
    expect(wrapper.emitted('get-activeRow')[0][0]).toStrictEqual(wrapper.vm.activeRow)
  })
})
