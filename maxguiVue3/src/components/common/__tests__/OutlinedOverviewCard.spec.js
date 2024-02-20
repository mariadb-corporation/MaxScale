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
import OutlinedOverviewCard from '@/components/common/OutlinedOverviewCard.vue'

describe('OutlinedOverviewCard', () => {
  let wrapper

  it(`Should add wrapperClass`, async () => {
    wrapper = mount(OutlinedOverviewCard, { props: { wrapperClass: 'test-class' } })
    expect(wrapper.find('.test-class').exists()).to.be.equal(true)
  })

  it(`Should add class to VCard`, () => {
    wrapper = mount(OutlinedOverviewCard, { attrs: { class: 'test-card-class' } })
    expect(wrapper.findComponent({ name: 'VCard' }).classes()).toContain('test-card-class')
  })

  it(`Should emit is-hovered event when card is hovered`, async () => {
    wrapper = mount(OutlinedOverviewCard, {
      props: { hoverableCard: true },
      attrs: { class: 'test-card-class' },
    })
    const vCard = wrapper.findComponent({ name: 'VCard' })
    await vCard.trigger('mouseenter')
    await vCard.trigger('mouseleave')
    expect(wrapper.emitted()['is-hovered'][0]).toBeTruthy
    expect(wrapper.emitted()['is-hovered'][1]).toBeFalsy
  })
})
