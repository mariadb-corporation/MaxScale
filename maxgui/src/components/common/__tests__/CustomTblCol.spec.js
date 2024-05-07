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
import CustomTblCol from '@/components/common/CustomTblCol.vue'
import { lodash } from '@/utils/helpers'

const mockProps = {
  columns: [{ cellProps: {} }],
  index: 0,
  name: 'name',
  value: 'example value',
  search: 'example',
}
const mountFactory = (opts) =>
  mount(
    CustomTblCol,
    lodash.merge(
      {
        props: mockProps,
      },
      opts
    )
  )

describe('CustomTblCol', () => {
  it('Computes the highlighter object based on value and search props', () => {
    const wrapper = mountFactory()
    const highlighter = wrapper.vm.highlighter
    expect(highlighter).toEqual({ keyword: 'example', txt: 'example value' })
  })

  it('Renders the slot content with correct props', () => {
    const wrapper = mountFactory({
      shallow: false,
      slots: { [`item.${mockProps.name}`]: '<div data-test="slot-content">Slot content</div>' },
    })
    expect(find(wrapper, 'slot-content').exists()).toBe(true)
  })
})
