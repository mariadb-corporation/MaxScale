/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import Servers from '@/pages/Dashboard/Servers'

import { findAnchorLinkInTable, getUniqueResourceNamesStub } from '@tests/unit/utils'
import { makeServer } from '@tests/unit/mirage/api'
import { initAllServers } from '@tests/unit/mirage/servers'
import { initAllMonitors } from '@tests/unit/mirage/monitors'
const expectedTableHeaders = [
    { text: `Monitor`, value: 'groupId', autoTruncate: true },
    { text: 'State', value: 'monitorState' },
    { text: 'Servers', value: 'id', autoTruncate: true },
    { text: 'Address', value: 'serverAddress', autoTruncate: true },
    { text: 'Port', value: 'serverPort' },
    { text: 'Connections', value: 'serverConnections', autoTruncate: true },
    { text: 'State', value: 'serverState' },
    { text: 'GTID', value: 'gtid' },
    { text: 'Services', value: 'serviceIds', autoTruncate: true },
]
const expectedTableRows = [
    {
        id: 'server_0',
        serverAddress: '127.0.0.1',
        serverPort: 4000,
        serverConnections: 0,
        serverState: 'Master, Running',
        serviceIds: ['Read-Only-Service', 'Read-Write-Service'],
        gtid: '0-1000-9',
        groupId: 'Monitor',
        monitorState: 'Running',
        showSlaveStats: true,
        showRepStats: false,
    },
    {
        id: 'server_1',
        serverAddress: '127.0.0.1',
        serverPort: 4001,
        serverConnections: 0,
        serverState: 'Slave, Running',
        serviceIds: ['Read-Only-Service', 'Read-Write-Service'],
        gtid: '0-1000-9',
        groupId: 'Monitor',
        monitorState: 'Running',
        showSlaveStats: false,
        showRepStats: true,
    },
    {
        id: 'server_2',
        serverAddress: '127.0.0.1',
        serverPort: 4002,
        serverConnections: 0,
        serverState: 'Slave, Running',
        serviceIds: ['Read-Write-Service'],
        gtid: '0-1000-9',
        groupId: 'Monitor',
        monitorState: 'Running',
        showSlaveStats: false,
        showRepStats: true,
    },
]

let api
let originalXMLHttpRequest = XMLHttpRequest
describe('Dashboard Servers tab', () => {
    let wrapper
    before(() => {
        api = makeServer({ environment: 'test' })
        initAllServers(api)
        initAllMonitors(api)
        // Force node to use the monkey patched window.XMLHttpRequest
        // This needs to come after `makeServer()` is called.
        // eslint-disable-next-line no-global-assign
        XMLHttpRequest = window.XMLHttpRequest
    })
    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: Servers,
        })
        await wrapper.vm.$store.dispatch('server/fetchAllServers')
        await wrapper.vm.$store.dispatch('monitor/fetchAllMonitors')
    })

    after(() => {
        api.shutdown()
        // Restore node's original window.XMLHttpRequest.
        // eslint-disable-next-line no-global-assign
        XMLHttpRequest = originalXMLHttpRequest
    })

    it(`Should process table rows accurately`, async () => {
        expect(wrapper.vm.tableRows).to.be.deep.equals(expectedTableRows)
    })

    it(`Should pass expected table headers to data-table`, async () => {
        const dataTable = wrapper.findComponent({ name: 'data-table' })
        expect(wrapper.vm.tableHeaders).to.be.deep.equals(expectedTableHeaders)
        expect(dataTable.vm.$props.headers).to.be.deep.equals(expectedTableHeaders)
    })

    it(`Should navigate to server detail page when a server is clicked`, async () => {
        const serverId = wrapper.vm.all_servers[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: serverId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'id'),
        })
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/servers/${serverId}`)
    })

    it(`Should navigate to service detail page when a service is clicked`, async () => {
        const serverId = wrapper.vm.all_servers[2].id // the last server has only one service
        const serviceId = wrapper.vm.all_servers[2].relationships.services.data[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: serverId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'serviceIds'),
        })
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
    })

    it(`Should navigate to monitor detail page when a monitor is clicked`, async () => {
        const serverId = wrapper.vm.all_servers[0].id
        const monitorId = wrapper.vm.all_servers[0].relationships.monitors.data[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: serverId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'groupId'),
        })
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/monitors/${monitorId}`)
    })

    it(`Should get total number of unique service names accurately`, async () => {
        const uniqueServiceNames = getUniqueResourceNamesStub(expectedTableRows, 'serviceIds')
        expect(wrapper.vm.$data.servicesLength).to.be.equals(uniqueServiceNames.length)
    })
})
