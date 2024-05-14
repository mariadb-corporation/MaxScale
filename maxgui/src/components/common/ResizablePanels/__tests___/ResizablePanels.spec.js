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
import ResizablePanels from '@/components/common/ResizablePanels/ResizablePanels.vue'

const defaultValue = 50
describe('ResizablePanels', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(ResizablePanels, {
      props: { modelValue: defaultValue, boundary: 1000, split: 'vert' },
    })
  })
  it(`Should pass accurate data to split-pane left`, () => {
    const { isLeft, split } = wrapper.findComponent('[data-test="pane-left"]').vm.$props
    expect(isLeft).toBe(true)
    expect(split).toBe(wrapper.vm.$props.split)
  })
  it(`Should pass accurate data to split-pane right`, () => {
    const { isLeft, split } = wrapper.findComponent('[data-test="pane-right"]').vm.$props
    expect(isLeft).toBe(false)
    expect(split).toBe(wrapper.vm.$props.split)
  })
  it(`Should pass accurate data to ResizeHandle`, () => {
    const { active, split } = wrapper.findComponent({ name: 'ResizeHandle' }).vm.$props
    expect(active).toBe(wrapper.vm.active)
    expect(split).toBe(wrapper.vm.$props.split)
  })
  it(`Should update currPct when modelValue props is changed in the parent component`, async () => {
    expect(wrapper.vm.currPct).toBe(defaultValue)
    await wrapper.setProps({ modelValue: 100 })
    expect(wrapper.vm.currPct).toBe(100)
  })
})
