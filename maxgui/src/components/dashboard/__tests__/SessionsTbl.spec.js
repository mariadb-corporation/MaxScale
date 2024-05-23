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
import SessionsTbl from '@/components/dashboard/SessionsTbl.vue'
import { createStore } from 'vuex'
import { dateFormat } from '@/utils/helpers'

const mockServiceObj = { id: 'service1', type: 'services' }

const mockSession = {
  attributes: {
    idle: 19.6,
    connected: 'Tue, 16 Apr 2024 12:00:00 GMT',
    user: 'maxskysql',
    remote: '127.0.0.1',
    memory: {},
    io_activity: 0,
    queries: [
      {
        command: 'COM_QUERY',
        completed: '2024-05-23T08:39:14.790',
        received: '2024-05-23T08:39:14.790',
        responses: [
          {
            duration: 0,
            server: 'server_1',
          },
        ],
        statement: 'SELECT 1',
      },
    ],
  },
  id: '202',
  relationships: {
    services: {
      data: [mockServiceObj],
    },
  },
}

const store = createStore({ state: { sessions: { current_sessions: [mockSession] } } })

describe('SessionsTbl', () => {
  let wrapper

  it('Pass expect data to SessionsTable', () => {
    wrapper = mount(SessionsTbl, { shallow: false })
    const {
      $attrs: { items, 'items-length': itemsLength },
      $props: { extraHeaders, hasLoading },
    } = wrapper.findComponent({ name: 'SessionsTable' }).vm
    expect(extraHeaders).toStrictEqual([wrapper.vm.SERVICE_HEADER])
    expect(items).toStrictEqual(wrapper.vm.items)
    expect(itemsLength).toBe(wrapper.vm.total_sessions)
    expect(hasLoading).toBe(false)
  })

  it('Computes items for table correctly', () => {
    wrapper = mount(SessionsTbl, {}, store)
    const expectedData = [
      {
        id: mockSession.id,
        user: `${mockSession.attributes.user}@${mockSession.attributes.remote}`,
        connected: dateFormat({ value: mockSession.attributes.connected }),
        idle: mockSession.attributes.idle,
        memory: mockSession.attributes.memory,
        io_activity: mockSession.attributes.io_activity,
        queries: mockSession.attributes.queries,
        serviceId: mockServiceObj.id,
      },
    ]
    expect(wrapper.vm.items).toStrictEqual(expectedData)
  })
})
