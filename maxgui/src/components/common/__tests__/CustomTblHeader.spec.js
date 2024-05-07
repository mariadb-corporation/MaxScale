/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import CustomTblHeader from '@/components/common/CustomTblHeader.vue'

describe('CustomTblHeader', () => {
  it('Renders the column title correctly', () => {
    const column = { title: 'Name' }
    const sortBy = { key: 'name', isDesc: false }
    const wrapper = mount(CustomTblHeader, { props: { column, sortBy } })
    expect(wrapper.text()).toContain('Name')
  })

  it('Displays the sorting icon when the column is sorted', () => {
    const column = { title: 'Name', headerProps: {} }
    const sortBy = { key: 'name', isDesc: false }
    const wrapper = mount(CustomTblHeader, { props: { column, sortBy } })
    expect(find(wrapper, 'sort-icon').exists()).toBe(true)
  })

  it('Displays the total count if showTotal prop is true', () => {
    const column = { title: 'Name', headerProps: {} }
    const sortBy = { key: 'name', isDesc: false }
    const wrapper = mount(CustomTblHeader, {
      props: { column, sortBy, total: 10, showTotal: true },
    })
    const ele = find(wrapper, 'total-count')
    expect(ele.exists()).toBe(true)
    expect(ele.text()).toContain('(10)')
  })
})
