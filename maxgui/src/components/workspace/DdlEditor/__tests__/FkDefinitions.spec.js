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
import FkDefinitions from '@wsComps/DdlEditor/FkDefinitions.vue'
import { lodash } from '@/utils/helpers'
import { CREATE_TBL_TOKENS as tokens } from '@/constants/workspace'

const mockFkObj = {
  cols: [{ id: 'col_3e0af061-3b54-11ee-a8e8-25db6da41f2a' }],
  id: 'key_3e0b1773-3b54-11ee-a8e8-25db6da41f2a',
  name: 'employees_ibfk_0',
  ref_cols: [{ id: 'col_3e0af062-3b54-11ee-a8e8-25db6da41f2a' }],
  ref_schema_name: 'company',
  ref_tbl_id: 'tbl_750b4681-3b5b-11ee-a3ad-dfd43862371d',
  on_delete: 'NO ACTION',
  on_update: 'NO ACTION',
}

const mountFactory = (opts) =>
  mount(
    FkDefinitions,
    lodash.merge(
      {
        shallow: false,
        props: {
          modelValue: {
            [tokens.foreignKey]: { [mockFkObj.id]: mockFkObj },
          },
          tableId: '',
          dim: { width: 1680, height: 1200 },
          lookupTables: {},
          newLookupTables: {},
          allLookupTables: [],
          allTableColMap: {},
          refTargets: [],
          tablesColNameMap: {},
          connData: {},
          charsetCollationMap: {},
        },
        global: { provide: { DDL_EDITOR_EMITTER_KEY: [] } },
      },
      opts
    )
  )

let wrapper

describe('FkDefinitions', () => {
  beforeEach(() => (wrapper = mountFactory()))

  it(`Should render VProgressLinear when isLoading is true`, async () => {
    wrapper.vm.isLoading = true
    await wrapper.vm.$nextTick()
    expect(wrapper.findComponent({ name: 'VProgressLinear' }).exists()).toBe(true)
  })

  it(`Should pass expected data to TblToolbar`, () => {
    const { selectedItems, isVertTable } = wrapper.findComponent({
      name: 'TblToolbar',
    }).vm.$props
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedItems)
    expect(isVertTable).toBe(wrapper.vm.isVertTable)
  })

  it(`Should pass accurate data to VirtualScrollTbl`, () => {
    const {
      headers,
      data,
      itemHeight,
      maxHeight,
      boundingWidth,
      showSelect,
      isVertTable,
      noDataText,
      selectedItems,
    } = wrapper.findComponent({
      name: 'VirtualScrollTbl',
    }).vm.$props
    expect(headers).toStrictEqual(wrapper.vm.headers)
    expect(data).toStrictEqual(wrapper.vm.rows)
    expect(itemHeight).toBe(32)
    expect(maxHeight).toBe(wrapper.vm.dim.height - wrapper.vm.headerHeight)
    expect(boundingWidth).toBe(wrapper.vm.dim.width)
    expect(showSelect).toBe(true)
    expect(noDataText).toStrictEqual(wrapper.vm.$t('noEntity', [wrapper.vm.$t('foreignKeys')]))
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedItems)
    expect(isVertTable).toBe(wrapper.vm.isVertTable)
  })

  it('Should return accurate number of headers', () => {
    expect(wrapper.vm.headers.length).toBe(7)
  })

  const twoWayBindingComputedProperties = {
    tmpLookupTables: 'newLookupTables',
    keyCategoryMap: 'modelValue',
  }
  Object.keys(twoWayBindingComputedProperties).forEach((property) => {
    let propName = twoWayBindingComputedProperties[property],
      evtName = `update:${propName}`
    it(`Should return accurate value for ${property}`, () => {
      expect(wrapper.vm[property]).toStrictEqual(wrapper.vm.$props[propName])
    })
    it(`Should emit ${evtName} event`, () => {
      wrapper.vm[property] = null
      expect(wrapper.emitted(evtName)[0]).toStrictEqual([null])
    })
  })

  const computedDataTypeMap = {
    array: ['fks', 'rows', 'unknownTargets', 'referencingColOptions'],
    object: ['plainKeyMap', 'plainKeyNameMap', 'fkMap', 'fkRefTblMap'],
  }
  Object.keys(computedDataTypeMap).forEach((type) => {
    computedDataTypeMap[type].forEach((property) => {
      it(`${property} should return an ${type}`, () => {
        if (type === 'object') expect(wrapper.vm[property]).toBeTypeOf(type)
        else expect(wrapper.vm[property]).toBeInstanceOf(Array)
      })
    })
  })

  it(`rows should be an 2d array`, () => {
    expect(wrapper.vm.rows).toBeInstanceOf(Array)
    wrapper.vm.rows.every((subArray) => expect(subArray).toBeInstanceOf(Array))
  })

  it('should emit update:modelValue event when stagingKeyCategoryMap changes', async () => {
    wrapper.vm.stagingKeyCategoryMap = {}
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('update:modelValue')[0][0]).toStrictEqual({})
  })
})
