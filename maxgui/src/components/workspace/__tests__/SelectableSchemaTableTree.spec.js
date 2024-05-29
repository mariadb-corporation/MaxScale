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
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import SelectableSchemaTableTree from '@wsComps/SelectableSchemaTableTree.vue'

describe('SelectableSchemaTableTree', () => {
  let wrapper
  beforeEach(
    () =>
      (wrapper = mount(SelectableSchemaTableTree, {
        props: {
          connId: '923a1239-d8fb-4991-b8f7-3ca3201c12f4',
          preselectedSchemas: [],
          triggerDataFetch: false,
          excludeNonFkSupportedTbl: false,
        },
      }))
  )

  it(`Should pass expected data to VirSchemaTree`, () => {
    const { expandedNodes, selectedNodes, data, loadChildren } = wrapper.findComponent({
      name: 'VirSchemaTree',
    }).vm.$props
    expect(expandedNodes).toStrictEqual(wrapper.vm.expandedNodes)
    expect(selectedNodes).toStrictEqual(wrapper.vm.selectedObjs)
    expect(data).toStrictEqual(wrapper.vm.data)
    expect(loadChildren).toStrictEqual(wrapper.vm.loadTables)
  })

  it(`Should render input message text`, async () => {
    const warningTxtStub = 'warning text'
    wrapper.vm.inputMsgObj = { type: 'warning', text: warningTxtStub }
    await wrapper.vm.$nextTick()
    expect(find(wrapper, 'input-msg').text()).toBe(warningTxtStub)
  })

  it(`Should render query error message text`, async () => {
    const queryErrMsgStub = 'query error text'
    wrapper.vm.queryErrMsg = queryErrMsgStub
    await wrapper.vm.$nextTick()
    expect(find(wrapper, 'query-err-msg').text()).toBe(queryErrMsgStub)
  })

  it(`Assert categorizeObjs contains expected properties`, () => {
    assert.containsAllKeys(wrapper.vm.categorizeObjs, ['emptySchemas', 'targets'])
  })

  const schemaNodeStub = { type: 'SCHEMA', name: 'information_schema' }
  const tblNodeStub = {
    parentNameData: { SCHEMA: 'test' },
    type: 'TABLE',
    name: 'QueryConn',
  }
  it(`categorizeObjs should includes empty schemas in emptySchemas property`, async () => {
    wrapper.vm.selectedObjs = [schemaNodeStub]
    await wrapper.vm.$nextTick()
    expect(wrapper.vm.categorizeObjs.emptySchemas[0]).toStrictEqual(schemaNodeStub.name)
  })

  it(`categorizeObjs should include accurate target objects`, async () => {
    wrapper.vm.selectedObjs = [tblNodeStub]
    await wrapper.vm.$nextTick()
    expect(wrapper.vm.categorizeObjs.targets[0]).toStrictEqual({
      tbl: tblNodeStub.name,
      schema: tblNodeStub.parentNameData.SCHEMA,
    })
  })
})
