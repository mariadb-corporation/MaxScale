/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import ColDefinitions from '@wsComps/DdlEditor/ColDefinitions.vue'
import {
  editorDataStub,
  charsetCollationMapStub,
  colKeyCategoryMapStub,
} from '@wsComps/DdlEditor/__tests__/stubData'
import { COL_ATTRS } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    ColDefinitions,
    lodash.merge(
      {
        props: {
          modelValue: editorDataStub.defs,
          initialData: lodash.cloneDeep(editorDataStub.defs),
          dim: { width: 1680, height: 1200 },
          defTblCharset: 'utf8mb4',
          defTblCollation: 'utf8mb4_general_ci',
          charsetCollationMap: charsetCollationMapStub,
          colKeyCategoryMap: colKeyCategoryMapStub,
        },
        global: { provide: { DDL_EDITOR_EMITTER_KEY: [] } },
      },
      opts
    )
  )

describe('ColDefinitions', () => {
  let wrapper

  beforeEach(() => (wrapper = mountFactory({ shallow: false })))

  it(`Should pass expected data to TblToolbar`, () => {
    const { selectedItems, isVertTable } = wrapper.findComponent({
      name: 'TblToolbar',
    }).vm.$props
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedItems)
    expect(isVertTable).toStrictEqual(wrapper.vm.isVertTable)
  })

  it('Should pass expected data to FilterList', () => {
    const { modelValue, items } = wrapper.findComponent({
      name: 'FilterList',
    }).vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.hiddenColSpecs)
    expect(items).toStrictEqual(wrapper.vm.COL_SPECS)
  })

  it(`Should pass expected data to VirtualScrollTbl`, () => {
    wrapper = mountFactory()
    const {
      headers,
      data,
      itemHeight,
      maxHeight,
      boundingWidth,
      showSelect,
      isVertTable,
      selectedItems,
    } = wrapper.findComponent({
      name: 'VirtualScrollTbl',
    }).vm.$props
    expect(headers).toStrictEqual(wrapper.vm.headers)
    expect(data).toStrictEqual(wrapper.vm.rows)
    expect(itemHeight).toBe(32)
    expect(maxHeight).toBe(wrapper.vm.tableMaxHeight)
    expect(boundingWidth).toBe(wrapper.vm.$props.dim.width)
    expect(showSelect).toBe(true)
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedItems)
    expect(isVertTable).toBe(wrapper.vm.isVertTable)
  })

  it('Should pass expected data to DataTypeInput', () => {
    const { modelValue, items } = wrapper.findComponent({
      name: 'DataTypeInput',
    }).vm.$attrs
    expect(modelValue).toBeTypeOf('string')
    expect(items).toStrictEqual(wrapper.vm.DATA_TYPE_ITEMS)
  })

  it('Should pass expected data to LazyInput', () => {
    const {
      $attrs: { modelValue },
      $props: { required },
    } = wrapper.findComponent({
      name: 'LazyInput',
    }).vm
    expect(modelValue).toBeTypeOf('string')
    expect(required).toBeTypeOf('boolean')
  })

  it('Should pass expected data to BoolInput', () => {
    const { modelValue, field, rowData } = wrapper.findComponent({
      name: 'BoolInput',
    }).vm.$props
    expect(modelValue).toBeTypeOf('boolean')
    expect(field).toBeTypeOf('string')
    expect(rowData).toBeInstanceOf(Array)
  })

  it('Should pass expected data to CharsetCollateInput', async () => {
    wrapper.vm.hiddenColSpecs = []
    await wrapper.vm.$nextTick()
    const {
      $props: { rowData, field, charsetCollationMap, defTblCharset, defTblCollation },
      $attrs: { modelValue },
    } = wrapper.findComponent({
      name: 'CharsetCollateInput',
    }).vm
    expect(modelValue).toBeTypeOf('string')
    expect(field).toBeTypeOf('string')
    expect(rowData).toBeInstanceOf(Array)
    expect(charsetCollationMap).toStrictEqual(wrapper.vm.$props.charsetCollationMap)
    expect(defTblCharset).toBe(wrapper.vm.$props.defTblCharset)
    expect(defTblCollation).toBe(wrapper.vm.$props.defTblCollation)
  })

  it(`Should return accurate value for data`, () => {
    expect(wrapper.vm.data).toStrictEqual(wrapper.vm.$props.modelValue)
  })

  it(`Should emit update:modelValue event`, () => {
    wrapper.vm.data = null
    expect(wrapper.emitted('update:modelValue')[0]).toStrictEqual([null])
  })

  it(`Should return accurate number of headers`, () => {
    expect(wrapper.vm.headers.length).toStrictEqual(14)
  })

  it(`Should return transformedCols with expected fields`, () => {
    expect(wrapper.vm.transformedCols).toBeInstanceOf(Array)
    expect(wrapper.vm.transformedCols.length).toBeGreaterThan(0)
    assert.containsAllKeys(wrapper.vm.transformedCols[0], ...Object.values(COL_ATTRS))
  })

  it(`rows should be an 2d array`, () => {
    expect(wrapper.vm.transformedCols).toBeInstanceOf(Array)
    expect(wrapper.vm.transformedCols.length).toBeGreaterThan(0)
    expect(wrapper.vm.rows.every((row) => Array.isArray(row))).toBe(true)
  })

  it(`Should handle deleteSelectedRows as expected`, async () => {
    // mock deleting all rows
    wrapper.vm.selectedItems = wrapper.vm.rows
    await wrapper.vm.$nextTick()
    wrapper.vm.deleteSelectedRows()
    expect(wrapper.emitted('update:modelValue')[0][0].col_map).toStrictEqual({})
  })

  it(`Should handle addNewCol as expected`, () => {
    wrapper.vm.addNewCol()
    const oldCols = Object.values(editorDataStub.defs.col_map)
    const newCols = Object.values(wrapper.emitted('update:modelValue')[0][0].col_map)
    expect(newCols.length).toStrictEqual(oldCols.length + 1)
    assert.containsAllKeys(newCols.at(-1), [
      'ai',
      'charset',
      'collate',
      'comment',
      'data_type',
      'default_exp',
      'generated',
      'id',
      'name',
      'nn',
      'un',
      'zf',
    ])
  })
})
