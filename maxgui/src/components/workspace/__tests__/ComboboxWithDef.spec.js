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
import ComboboxWithDef from '@wsComps/ComboboxWithDef.vue'

describe('ComboboxWithDef', () => {
  let wrapper

  it('Should display the "(default)" label next to the defItem', () => {
    const items = ['item1', 'item2', 'item3']
    wrapper = mount(ComboboxWithDef, {
      shallow: false,
      attrs: { items, modelValue: '', menuProps: { modelValue: true, attach: true } },
      props: { defItem: 'item2' },
    })
    const defItem = find(wrapper, 'def-item-label')
    expect(defItem.exists()).toBe(true)
    expect(defItem.text()).toBe(`(${wrapper.vm.$t('def')})`)
  })
})
