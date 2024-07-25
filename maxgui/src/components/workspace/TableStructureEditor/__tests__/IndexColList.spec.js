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
import IndexColList from '@wsComps/TableStructureEditor/IndexColList.vue'
import {
  editorDataStub,
  tableColNameMapStub,
  tableColMapStub,
} from '@wsComps/TableStructureEditor/__tests__/stubData'
import { CREATE_TBL_TOKENS as tokens } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'

const stubKeyId = Object.keys(editorDataStub.defs.key_category_map[tokens.primaryKey])[0]

const mountFactory = (opts) =>
  mount(
    IndexColList,
    lodash.merge(
      {
        shallow: false,
        props: {
          modelValue: editorDataStub.defs.key_category_map,
          dim: { width: 1680, height: 1200 },
          keyId: stubKeyId,
          category: tokens.primaryKey,
          tableColNameMap: tableColNameMapStub,
          tableColMap: tableColMapStub,
        },
      },
      opts
    )
  )

describe('IndexColList', () => {
  let wrapper

  beforeEach(async () => {
    wrapper = mountFactory()
  })

  it(`Should pass expected data to VirtualScrollTbl`, () => {
    const {
      headers,
      data,
      itemHeight,
      maxHeight,
      boundingWidth,
      showSelect,
      selectedItems,
      showRowCount,
    } = wrapper.findComponent({
      name: 'VirtualScrollTbl',
    }).vm.$props
    expect(headers).toStrictEqual(wrapper.vm.HEADERS)
    expect(data).toStrictEqual(wrapper.vm.rows)
    expect(itemHeight).toBe(32)
    expect(maxHeight).toBe(wrapper.vm.dim.height)
    expect(boundingWidth).toBe(wrapper.vm.dim.width)
    expect(showSelect).toBe(true)
    expect(showRowCount).toBe(false)
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedItems)
  })

  it(`Should return accurate number of headers`, () => {
    expect(wrapper.vm.HEADERS.length).toStrictEqual(6)
  })

  it(`Should return accurate value for keyCategoryMap`, () => {
    expect(wrapper.vm.keyCategoryMap).toStrictEqual(wrapper.vm.$props.modelValue)
  })

  it(`Should emit update:modelValue event when keyCategoryMap is changed`, async () => {
    wrapper.vm.keyCategoryMap = null
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('update:modelValue')[0][0]).toBe(null)
  })
})
