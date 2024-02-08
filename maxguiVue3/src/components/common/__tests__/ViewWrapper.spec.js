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
import ViewWrapper from '@/components/common/ViewWrapper.vue'

describe('ViewWrapper', () => {
  it('renders correctly with default props', () => {
    const wrapper = mount(ViewWrapper)
    expect(wrapper.exists()).toBe(true)
    expect(wrapper.classes()).toContain('view-wrapper', 'overflow-auto')
    expect(wrapper.find('.view-wrapper__container').exists()).toBe(true)
    expect(wrapper.find('.view-header-container').exists()).toBe(true)
  })

  it('does not render with overflow-auto class when overflow prop is false', () => {
    const wrapper = mount(ViewWrapper, { props: { overflow: false } })
    expect(wrapper.classes()).not.toContain('overflow-auto')
  })

  it('renders with view-wrapper__container__fluid class when fluid prop is true', () => {
    const wrapper = mount(ViewWrapper, { props: { fluid: true } })
    expect(wrapper.find('.view-wrapper__container').classes()).toContain(
      'view-wrapper__container__fluid'
    )
  })

  it('renders with custom spacer style when spacerStyle prop is provided', () => {
    const wrapper = mount(ViewWrapper, {
      props: { spacerStyle: { borderBottom: 'thin solid #e7eef1' } },
    })
    expect(wrapper.findComponent({ name: 'v-spacer' }).attributes('style')).toBe(
      'border-bottom: thin solid #e7eef1;'
    )
  })
})
