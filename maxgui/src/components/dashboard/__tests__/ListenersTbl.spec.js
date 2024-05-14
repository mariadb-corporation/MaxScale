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
import ListenersTbl from '@/components/dashboard/ListenersTbl.vue'
import { createStore } from 'vuex'

const mockServiceObj = { id: 'service1', type: 'services' }

const mockListener = {
  id: 'listener1',
  attributes: {
    state: 'Running',
    parameters: { port: 9000, address: '::', socket: null },
  },
  relationships: { services: { data: [mockServiceObj] } },
}

const store = createStore({ state: { listeners: { all_objs: [mockListener] } } })

describe('ListenersTbl', () => {
  let wrapper

  it('Pass expect data to OverviewTbl', () => {
    wrapper = mount(ListenersTbl, { shallow: false })
    const {
      $attrs: { headers },
      $props: { data, totalMap },
    } = wrapper.findComponent({ name: 'OverviewTbl' }).vm
    expect(headers).toStrictEqual(wrapper.vm.HEADERS)
    expect(data).toStrictEqual(wrapper.vm.items)
    expect(totalMap).toStrictEqual(wrapper.vm.totalMap)
  })

  it('Computes items for table correctly', () => {
    wrapper = mount(ListenersTbl, {}, store)
    const expectedData = [
      {
        id: mockListener.id,
        address: mockListener.attributes.parameters.address,
        port: mockListener.attributes.parameters.port,
        state: mockListener.attributes.state,
        serviceId: mockServiceObj.id,
      },
    ]
    expect(wrapper.vm.items).toStrictEqual(expectedData)
  })
})
