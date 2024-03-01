/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import CollapsibleReadOnlyTbl from '@/components/common/CollapsibleReadOnlyTbl.vue'

const mockTreeData = { a: { b: { c: 'value' } } }
const mockTitle = 'CollapsibleReadOnlyTbl title'
const mockAttrs = { data: mockTreeData }

describe('CollapsibleReadOnlyTbl.vue', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(CollapsibleReadOnlyTbl, {
      shallow: false,
      props: { title: mockTitle },
      attrs: mockAttrs,
    })
  })

  it('Should pass expected data to CollapsibleCtr', () => {
    expect(wrapper.findComponent({ name: 'CollapsibleCtr' }).vm.$props.title).toBe(mockTitle)
  })

  it('Should pass expected data to TreeTable', () => {
    const {
      $props: { data },
      $attrs: { loading, search },
    } = wrapper.findComponent({ name: 'TreeTable' }).vm
    expect(search).toBe(wrapper.vm.search_keyword)
    expect(loading).toBe(wrapper.vm.loading)
    expect(data).toStrictEqual(mockTreeData)
  })
})
