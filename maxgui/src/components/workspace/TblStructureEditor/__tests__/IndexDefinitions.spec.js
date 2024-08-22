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
import IndexDefinitions from '@wsComps/TblStructureEditor/IndexDefinitions.vue'
import {
  editorDataStub,
  tableColNameMapStub,
  tableColMapStub,
} from '@wsComps/TblStructureEditor/__tests__/stubData'
import { CREATE_TBL_TOKEN_MAP as tokens } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    IndexDefinitions,
    lodash.merge(
      {
        shallow: false,
        props: {
          modelValue: editorDataStub.defs.key_category_map,
          dim: { width: 1680, height: 1200 },
          tableColNameMap: tableColNameMapStub,
          tableColMap: tableColMapStub,
        },
      },
      opts
    )
  )

describe('IndexDefinitions', () => {
  let wrapper

  beforeEach(() => {
    wrapper = mountFactory()
  })

  it(`Should pass expected data to TblToolbar`, () => {
    const { selectedItems, showRotateTable, reverse } = wrapper.findComponent({
      name: 'TblToolbar',
    }).vm.$props
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedItems)
    expect(showRotateTable).toBe(false)
    expect(reverse).toBe(true)
  })

  it('Should pass expected data to IndexList', () => {
    const { modelValue, selectedItems } = wrapper.findComponent({
      name: 'IndexList',
    }).vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.stagingKeyCategoryMap)
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedItems)
  })

  it(`Should pass expected data to IndexColList`, () => {
    const { modelValue, keyId, category, tableColNameMap, tableColMap } = wrapper.findComponent({
      name: 'IndexColList',
    }).vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.stagingKeyCategoryMap)
    expect(keyId).toBe(wrapper.vm.selectedKeyId)
    expect(category).toBe(wrapper.vm.selectedKeyCategory)
    expect(tableColNameMap).toStrictEqual(tableColNameMapStub)
    expect(tableColMap).toStrictEqual(tableColMapStub)
  })

  it(`Should not render IndexColList when there is no selectedKeyId`, async () => {
    wrapper.vm.selectedItems = []
    await wrapper.vm.$nextTick()
    expect(wrapper.findComponent({ name: 'IndexColList' }).exists()).toBe(false)
  })

  it(`Should return accurate value for keyCategoryMap`, () => {
    expect(wrapper.vm.keyCategoryMap).toStrictEqual(wrapper.vm.$props.modelValue)
  })

  it(`Should emit update:modelValue event`, () => {
    wrapper.vm.keyCategoryMap = null
    expect(wrapper.emitted('update:modelValue')[0][0]).toBe(null)
  })

  it(`selectedItem should be an array`, () => {
    expect(wrapper.vm.selectedItem).toBeInstanceOf(Array)
  })

  function getKeysByCategory({ wrapper, category }) {
    return Object.values(wrapper.vm.stagingKeyCategoryMap[category] || {})
  }
  it(`Should handle addNewKey as expected`, () => {
    wrapper = mountFactory()
    const oldPlainKeys = getKeysByCategory({ wrapper, category: tokens.key })
    wrapper.vm.addNewKey()
    const newPlainKeys = getKeysByCategory({ wrapper, category: tokens.key })
    expect(newPlainKeys.length).toStrictEqual(oldPlainKeys.length + 1)
    assert.containsAllKeys(newPlainKeys.at(-1), ['id', 'cols', 'name'])
  })
})
