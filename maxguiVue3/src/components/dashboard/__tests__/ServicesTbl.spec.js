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
import ServicesTbl from '@/components/dashboard/ServicesTbl.vue'
import { createStore } from 'vuex'

const mockRoutingTarget = { id: 'server_0', type: 'servers' }

const mockService = {
  id: 'service1',
  attributes: { state: 'running', router: 'router1', connections: 10, total_connections: 20 },
  relationships: { servers: { data: [mockRoutingTarget] } },
}

const store = createStore({ state: { services: { all_objs: [mockService] } } })

describe('ServicesTbl', () => {
  let wrapper

  it('Pass expect data to OverviewTbl', () => {
    wrapper = mount(ServicesTbl)
    const {
      $attrs: { headers, 'filter-mode': filterMode },
      $props: { data, totalMap },
    } = wrapper.findComponent({ name: 'OverviewTbl' }).vm
    expect(filterMode).toBe('some')
    expect(headers).toStrictEqual(wrapper.vm.HEADERS)
    expect(data).toStrictEqual(wrapper.vm.items)
    expect(totalMap).toStrictEqual(wrapper.vm.totalMap)
  })

  it('Computes items for table correctly', () => {
    wrapper = mount(ServicesTbl, {}, store)
    const expectedData = [
      {
        id: mockService.id,
        ...mockService.attributes,
        routingTargets: [mockRoutingTarget],
      },
    ]
    expect(wrapper.vm.items).toStrictEqual(expectedData)
  })
})
