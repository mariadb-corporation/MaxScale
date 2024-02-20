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
import TblToolbar from '@/components/workspace/DdlEditor/TblToolbar.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    TblToolbar,
    lodash.merge(
      {
        propsData: {
          selectedItems: [],
          isVertTable: true,
          showRotateTable: true,
          reverse: false,
        },
      },
      opts
    )
  )

let wrapper

describe('TblToolbar', () => {
  describe(`Child component's data communication tests`, () => {
    it(`Should conditionally add 'flex-row-reverse' class`, async () => {
      wrapper = mountFactory()
      expect(wrapper.classes()).to.not.includes('flex-row-reverse')
      await wrapper.setProps({ reverse: true })
      expect(wrapper.classes()).to.includes('flex-row-reverse')
    })
    it(`Should conditionally render delete button`, async () => {
      expect(wrapper.findComponent('.delete-btn').exists()).to.be.false
      await wrapper.setProps({ selectedItems: ['a', 'b'] })
      expect(wrapper.findComponent('.delete-btn').exists()).to.be.true
    })
    it(`Should conditionally render delete button`, async () => {
      expect(wrapper.findComponent('.rotate-btn').exists()).to.be.true
      await wrapper.setProps({ showRotateTable: false })
      expect(wrapper.findComponent('.rotate-btn').exists()).to.be.false
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
      expect(wrapper.vm.isVertTableMode).to.be.eql(wrapper.vm.$props.isVertTable)
    })

    it('Should emit update:isVertTable event', () => {
      wrapper = mountFactory()
      wrapper.vm.isVertTableMode = false
      expect(wrapper.emitted('update:isVertTable')[0]).to.be.eql([false])
    })
  })
})
