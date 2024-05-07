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
import ServersTbl from '@/components/dashboard/ServersTbl.vue'
import { createStore } from 'vuex'

const mockServer = {
  id: 1,
  attributes: {
    state: 'Running',
    parameters: { address: '127.0.0.1', port: 3306, socket: null },
    statistics: { connections: 10 },
    gtid_current_pos: '0-1000-1',
  },
  relationships: {
    services: { data: [] },
    monitors: { data: [{ id: 'monitor1' }] },
  },
}
const mockMonitor = {
  id: 'monitor1',
  attributes: {
    state: 'state1',
    module: 'module1',
    monitor_diagnostics: { master: 'master1', server_info: [{ name: 'server1' }] },
  },
}

const store = createStore({
  state: { servers: { all_objs: [mockServer] } },
  getters: {
    'monitors/monitorsMap': () => ({ [mockMonitor.id]: mockMonitor }),
    'monitors/total': () => 1,
  },
})

describe('ServersTbl', () => {
  let wrapper

  it('Pass expect data to VDataTable', () => {
    wrapper = mount(ServersTbl)
    const { headers, items, search, itemsPerPage } = wrapper.findComponent({ name: 'VDataTable' })
      .vm.$props
    expect(headers).toStrictEqual(wrapper.vm.HEADERS)
    expect(items).toStrictEqual(wrapper.vm.items)
    expect(search).toBe(wrapper.vm.search_keyword)
    expect(itemsPerPage).toBe(-1)
  })

  it('Computes data for table correctly', () => {
    wrapper = mount(ServersTbl, {}, store)
    const expectedData = [
      {
        id: mockServer.id,
        serverAddress: `${mockServer.attributes.parameters.address}:${mockServer.attributes.parameters.port}`,
        serverConnections: mockServer.attributes.statistics.connections,
        serverState: mockServer.attributes.state,
        serviceIds: 'noEntity',
        gtid: mockServer.attributes.gtid_current_pos,
        monitorId: mockMonitor.id,
        monitorState: mockMonitor.attributes.state,
      },
    ]
    expect(wrapper.vm.data).toStrictEqual(expectedData)
  })

  it('Renders CustomTblHeader', () => {
    wrapper = mount(ServersTbl, { shallow: false })
    expect(wrapper.findAllComponents({ name: 'CustomTblHeader' }).length).toBe(
      wrapper.vm.HEADERS.length
    )
  })

  it('Call toggleSortBy on header click', async () => {
    wrapper = mount(ServersTbl, { shallow: false })
    const spy = vi.spyOn(wrapper.vm, 'toggleSortBy')
    await wrapper.findComponent({ name: 'CustomTblHeader' }).trigger('click')
    expect(spy).toHaveBeenCalled()
  })
})
