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
import CtxMenu from '@/components/common/CtxMenu.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    CtxMenu,
    lodash.merge(
      {
        shallow: false,
        props: { items: [] },
        // mock menu opens
        data: () => ({ menuOpen: true }),
        attrs: { attach: true },
      },
      opts
    )
  )

describe('CtxMenu', () => {
  let wrapper
  it('Should emit item-click event on clicking child menu item', async () => {
    const items = [{ text: 'Item 1' }, { text: 'Item 2' }]
    wrapper = mountFactory({ props: { items } })
    const menuItems = wrapper.findAll('[data-test="child-menu-item"]')
    // Click the first menu item
    await menuItems.at(0).trigger('click')
    expect(wrapper.emitted('item-click')[0]).to.deep.equal([{ text: 'Item 1' }])
  })

  it(`Should return accurate modelValue for isOpened`, () => {
    wrapper = mountFactory()
    expect(wrapper.vm.isOpened).to.be.eql(wrapper.vm.$attrs.modelValue)
  })

  it(`Should emit update:modelValue event`, () => {
    wrapper = mountFactory({ attrs: { modelValue: true } })
    wrapper.vm.isOpened = false
    expect(wrapper.emitted('update:modelValue')[0]).to.be.eql([false])
  })
})
