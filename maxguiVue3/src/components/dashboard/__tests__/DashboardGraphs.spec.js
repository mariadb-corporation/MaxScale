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
import DashboardGraphs from '@/components/dashboard/DashboardGraphs.vue'

describe('DashboardGraphs', () => {
  let wrapper
  beforeEach(() => (wrapper = mount(DashboardGraphs)))
  it('Renders three chart cards', () => {
    const chartCards = wrapper.findAllComponents({ name: 'OutlinedOverviewCard' })
    expect(chartCards.length).to.be.equals(3)
  })

  it('Render a button to toggle graph height', () => {
    expect(find(wrapper, 'toggle-expansion-btn').exists()).toBeTruthy()
  })

  it('Change graph are_dsh_graphs_expanded state when toggle-expansion-btn is clicked', async () => {
    const oldVal = wrapper.vm.are_dsh_graphs_expanded
    await find(wrapper, 'toggle-expansion-btn').trigger('click')
    expect(wrapper.vm.are_dsh_graphs_expanded).toBe(!oldVal)
  })

  it('Render a button to open graph config dialog', () => {
    expect(find(wrapper, 'setting-btn').exists()).toBeTruthy()
  })

  it('Open graph config dialog when setting-btn is clicked', async () => {
    await find(wrapper, 'setting-btn').trigger('click')
    expect(wrapper.vm.isDlgOpened).toBeTruthy()
  })
})
