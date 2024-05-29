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
import WkeNavCtr from '@wsComps/WkeNavCtr.vue'

describe('WkeNavCtr', () => {
  let wrapper

  beforeEach(() => (wrapper = mount(WkeNavCtr, { props: { height: 500 } })))

  it('Should pass expected data to VTabs', () => {
    const { modelValue, showArrows, hideSlider, height, centerActive } = wrapper.findComponent({
      name: 'VTabs',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.activeWkeID)
    expect(showArrows).toBe(true)
    expect(hideSlider).toBe(true)
    expect(height).toBe(wrapper.vm.$props.height)
    expect(centerActive).toBe(true)
  })

  it('Should render WkeToolbar with expected height', () => {
    const wkeToolbar = wrapper.findComponent({ name: 'WkeToolbar' })
    expect(wkeToolbar.exists()).toBe(true)
    expect(wkeToolbar.attributes('style')).toContain(`height: ${wrapper.vm.$props.height}px`)
  })
})
