/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import DashboardView from '@/views/DashboardView.vue'

describe('DashboardView', () => {
  let wrapper

  beforeEach(() => {
    wrapper = mount(DashboardView, { shallow: false, global: { stubs: { StreamLineChart: true } } })
  })

  it('Renders Page Header', () => {
    expect(wrapper.findComponent({ name: 'ViewHeader' }).exists()).toBe(true)
  })

  it('Renders DashboardGraphs', () => {
    expect(wrapper.findComponent({ name: 'DashboardGraphs' }).exists()).toBe(true)
  })
})
