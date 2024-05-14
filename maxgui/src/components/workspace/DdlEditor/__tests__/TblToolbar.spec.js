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
import { find } from '@/tests/utils'
import TblToolbar from '@wsComps/DdlEditor/TblToolbar.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    TblToolbar,
    lodash.merge(
      {
        props: {
          selectedItems: [],
          isVertTable: true,
          showRotateTable: true,
          reverse: false,
        },
        global: { stubs: { TooltipBtn: true } },
      },
      opts
    )
  )

let wrapper

describe('TblToolbar', () => {
  describe(`Child component's data communication tests`, () => {
    it(`Should render add button`, () => {
      wrapper = mountFactory()
      expect(find(wrapper, 'add-btn').exists()).toBe(true)
    })
    it(`Should conditionally add 'flex-row-reverse' class`, async () => {
      wrapper = mountFactory()
      assert.notInclude(wrapper.classes(), 'flex-row-reverse')
      await wrapper.setProps({ reverse: true })
      assert.include(wrapper.classes(), 'flex-row-reverse')
    })
    it(`Should conditionally render delete button`, async () => {
      expect(find(wrapper, 'delete-btn').exists()).toBe(false)
      await wrapper.setProps({ selectedItems: ['a', 'b'] })
      expect(find(wrapper, 'delete-btn').exists()).toBe(true)
    })
    it(`Should conditionally render rotate button`, async () => {
      expect(find(wrapper, 'rotate-btn').exists()).toBe(true)
      await wrapper.setProps({ showRotateTable: false })
      expect(find(wrapper, 'rotate-btn').exists()).toBe(false)
    })
    it('renders append slot content', () => {
      wrapper = mountFactory({
        slots: { append: '<div>toolbar-append-content</div>' },
      })
      expect(wrapper.html()).contains('<div>toolbar-append-content</div>')
    })
  })

  describe(`Computed properties tests`, () => {
    it('Should return accurate value for isVertTableMode', () => {
      wrapper = mountFactory()
      expect(wrapper.vm.isVertTableMode).toBe(wrapper.vm.$props.isVertTable)
    })

    it('Should emit update:isVertTable event', () => {
      wrapper = mountFactory()
      wrapper.vm.isVertTableMode = false
      expect(wrapper.emitted('update:isVertTable')[0][0]).toBe(false)
    })
  })
})
