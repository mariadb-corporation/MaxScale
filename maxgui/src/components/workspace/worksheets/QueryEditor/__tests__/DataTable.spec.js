/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import DataTable from '@wkeComps/QueryEditor/DataTable.vue'
import { MAX_RENDERED_COLUMNS } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'

const dataStub = {
  fields: ['Field 1', 'Field 2'],
  data: [
    ['Row 1 Col 1', 'Row 1 Col 2'],
    ['Row 2 Col 1', 'Row 2 Col 2'],
  ],
}

const mountFactory = (opts) =>
  mount(
    DataTable,
    lodash.merge(
      {
        shallow: false,
        props: { data: dataStub, height: 300, width: 600 },
      },
      opts
    )
  )

describe(`DataTable`, () => {
  let wrapper

  it('Should initialize computed properties correctly', () => {
    wrapper = mountFactory()

    const expectedHeaders = dataStub.fields.map((field) => ({ text: field }))

    expect(wrapper.vm.headers).toStrictEqual(expectedHeaders)
    expect(wrapper.vm.fields).toStrictEqual(dataStub.fields)
    expect(wrapper.vm.tableHeaders).toStrictEqual([
      { text: '#', maxWidth: 'max-content', hidden: false },
      ...expectedHeaders.map((h, i) => ({
        ...h,
        resizable: true,
        draggable: wrapper.vm.$props.draggableCell,
        hidden: wrapper.vm.hiddenHeaderIndexes.includes(i + 1),
        useCellSlot: h.useCellSlot,
      })),
    ])
    expect(wrapper.vm.tableRows).toStrictEqual(dataStub.data.map((row, i) => [i + 1, ...row]))
  })

  it('Should handle toolbar visibility based on hideToolbar prop', async () => {
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'DataTableToolbar' }).exists()).toBe(true)
    await wrapper.setProps({ hideToolbar: true })
    expect(wrapper.findComponent({ name: 'DataTableToolbar' }).exists()).toBe(false)
  })

  it('should emit get-table-headers event with correct headers', () => {
    const customHeaders = [{ text: 'Custom Header 1' }, { text: 'Custom Header 2' }]
    wrapper = mountFactory({ props: { customHeaders } })

    const expectedEmittedHeaders = [
      { text: '#', maxWidth: 'max-content', hidden: false },
      ...customHeaders.map((h, i) => ({
        ...h,
        resizable: true,
        draggable: wrapper.vm.$props.draggableCell,
        hidden: wrapper.vm.hiddenHeaderIndexes.includes(i + 1),
        useCellSlot: h.useCellSlot,
      })),
    ]

    expect(wrapper.emitted('get-table-headers')).toBeTruthy()
    expect(wrapper.emitted('get-table-headers')[0][0]).toStrictEqual(expectedEmittedHeaders)
  })

  it('Should correctly handle context menu visibility', () => {
    wrapper = mountFactory()
    const activatorID = 'test-id'

    wrapper.vm.contextmenuHandler({ activatorID })

    expect(wrapper.vm.showCtxMenu).toBe(true)
    expect(wrapper.vm.ctxMenuData).toEqual({ activatorID })

    // Trigger the same context menu to close it
    wrapper.vm.contextmenuHandler({ activatorID })

    expect(wrapper.vm.showCtxMenu).toBe(false)
    expect(wrapper.vm.ctxMenuData).toEqual({})
  })

  it('Should automatically hide headers if it exceeds MAX_RENDERED_COLUMNS', () => {
    wrapper = mountFactory({
      props: { customHeaders: Array(MAX_RENDERED_COLUMNS + 1).fill({ text: 'Header' }) },
    })
    expect(wrapper.vm.hiddenHeaderIndexes.length).toBeGreaterThan(0)
  })
})
