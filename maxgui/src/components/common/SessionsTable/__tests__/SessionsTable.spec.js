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
import SessionsTable from '@/components/common/SessionsTable/SessionsTable.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(SessionsTable, lodash.merge({ attrs: { itemsLength: 10 } }, opts))

describe('SessionsTable', () => {
  let wrapper

  it('Should pass expected data to VDataTableServer', () => {
    wrapper = mountFactory()
    const { page, itemsPerPage, loading, noDataText, itemsPerPageOptions, headers } =
      wrapper.findComponent({
        name: 'VDataTableServer',
      }).vm.$props
    expect(page).toBe(wrapper.vm.pagination.page)
    expect(loading).toBe(wrapper.vm.$props.hasLoading ? wrapper.vm.loading : false)
    expect(itemsPerPage).toBe(wrapper.vm.pagination.itemsPerPage)
    expect(noDataText).toBe(wrapper.vm.$t('noEntity', [wrapper.vm.$t('sessions', 2)]))
    expect(itemsPerPageOptions).toBe(wrapper.vm.itemsPerPageOptions)
    expect(headers).toBe(wrapper.vm.headers)
  })

  it('Should merge extraHeaders props to headers', () => {
    const extraHeadersStub = [{ title: 'Test Header', value: 'test' }]
    wrapper = mountFactory({ props: { extraHeaders: extraHeadersStub } })

    extraHeadersStub.forEach((header) => {
      expect(wrapper.vm.headers.some((item) => lodash.isEqual(item, header))).toBeTruthy()
    })
  })

  it('Should compute default headers correctly', () => {
    wrapper = mountFactory()
    const expectedKeys = ['id', 'user', 'connected', 'idle', 'memory', 'io_activity']
    expect(wrapper.vm.headers).toHaveLength(6)
    expect(wrapper.vm.headers.map((h) => h.value)).toStrictEqual(expectedKeys)
  })

  it('Should emit confirm-kill event with session id when confirmKill', () => {
    wrapper = mountFactory()
    wrapper.vm.confDlg.item = { id: 123 }
    wrapper.vm.confirmKill()
    expect(wrapper.emitted('confirm-kill')).toBeTruthy()
    expect(wrapper.emitted('confirm-kill')[0][0]).toBe(123)
  })

  it('Should correctly render session data', () => {
    const sessionData = [
      {
        id: 1,
        user: 'maxscale',
        connected: '2022-05-01T10:00:00',
        idle: 60,
        memory: { total: 66868 },
        io_activity: 10,
      },
      {
        id: 2,
        user: 'mariadb',
        connected: '2022-05-01T11:00:00',
        idle: 120,
        memory: { total: 66868 },
        io_activity: 20,
      },
    ]
    wrapper = mountFactory({ shallow: false, attrs: { items: sessionData } })
    const tableRows = wrapper.findAll('.sessions-table tbody tr')
    expect(tableRows).toHaveLength(sessionData.length)
    tableRows.forEach((row, index) => {
      const session = sessionData[index]
      const cells = row.findAll('td')
      expect(cells).toHaveLength(6)
      expect(cells[0].text()).toBe(session.id.toString())
      expect(cells[1].text()).toBe(session.user)
      expect(cells[2].text()).toBe(session.connected)
      expect(cells[3].text()).toBe(session.idle.toString())
      expect(cells[4].findComponent({ name: 'MemoryCell' }).exists()).toBe(true)
      expect(cells[5].text()).toBe(session.io_activity.toString())
    })
  })

  it('Should correctly set confDlg.item when onKillSession method is called', () => {
    wrapper = mountFactory()
    const item = { id: 'abc123', user: 'mariadb' }
    wrapper.vm.onKillSession(item)
    expect(wrapper.vm.confDlg).toStrictEqual({ isOpened: true, item })
  })
})
