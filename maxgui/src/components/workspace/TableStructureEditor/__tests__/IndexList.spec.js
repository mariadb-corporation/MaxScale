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
import IndexList from '@wsComps/TableStructureEditor/IndexList.vue'
import { editorDataStub } from '@wsComps/TableStructureEditor/__tests__/stubData'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    IndexList,
    lodash.merge(
      {
        props: {
          modelValue: editorDataStub.defs.key_category_map,
          dim: { width: 1680, height: 1200 },
          selectedItems: [],
        },
      },
      opts
    )
  )

describe('IndexList', () => {
  let wrapper

  beforeEach(async () => {
    wrapper = mountFactory()
    await wrapper.vm.$nextTick()
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
      singleSelect,
      noDataText,
      selectedItems,
    } = wrapper.findComponent({
      name: 'VirtualScrollTbl',
    }).vm.$props
    expect(headers).toStrictEqual(wrapper.vm.HEADERS)
    expect(data).toStrictEqual(wrapper.vm.keyItems)
    expect(itemHeight).toBe(32)
    expect(maxHeight).toBe(wrapper.vm.dim.height)
    expect(boundingWidth).toBe(wrapper.vm.dim.width)
    expect(showSelect).toBe(true)
    expect(singleSelect).toBe(true)
    expect(noDataText).toStrictEqual(wrapper.vm.$t('noEntity', [wrapper.vm.$t('indexes')]))
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedRows)
  })

  it(`Should return accurate number of headers`, () => {
    expect(wrapper.vm.HEADERS.length).toStrictEqual(4)
  })

  it(`Should return accurate value for keyCategoryMap`, () => {
    expect(wrapper.vm.keyCategoryMap).toStrictEqual(wrapper.vm.$props.modelValue)
  })

  it(`Should emit input event`, async () => {
    wrapper.vm.keyCategoryMap = null
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('update:modelValue')[0][0]).toBe(null)
  })
})
