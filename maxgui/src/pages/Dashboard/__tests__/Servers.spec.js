/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

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
        isMaster: true,
        serverInfo: [
            {
                gtid_binlog_pos: '0-1000-9',
                gtid_current_pos: '0-1000-9',
                lock_held: null,
                master_group: null,
                name: 'server_1_with_longgggggggggggggggggggggggggggggggggg_name',
                read_only: false,
                server_id: 1001,
                slave_connections: [],
            },
        ],
    },
    {
        id: 'server_1_with_longgggggggggggggggggggggggggggggggggg_name',
        serverAddress: '127.0.0.1',
        serverPort: 4001,
        serverConnections: 0,
        serverState: 'Slave, Running',
        serviceIds: ['Read-Only-Service'],
        gtid: '0-1000-9',
        groupId: 'Monitor',
        monitorState: 'Running',
        isSlave: true,
        serverInfo: [
            {
                gtid_binlog_pos: '0-1000-9',
                gtid_current_pos: '0-1000-9',
                lock_held: null,
                master_group: null,
                name: 'server_1_with_longgggggggggggggggggggggggggggggggggg_name',
                read_only: false,
                server_id: 1001,
                slave_connections: [],
            },
        ],
    },
    {
        id: 'server_2',
        serverAddress: '127.0.0.1',
        serverPort: 4002,
        serverConnections: 0,
        serverState: 'Down',
        serviceIds: 'No services',
        gtid: undefined,
        groupId: 'Not monitored',
        monitorState: '',
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

    it(`Should process table rows accurately`, () => {
        expect(wrapper.vm.tableRows).to.be.deep.equals(expectedTableRows)
    })

    it(`Should pass expected table headers to data-table`, () => {
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
        const serverId = wrapper.vm.all_servers[1].id
        const serviceId = wrapper.vm.all_servers[1].relationships.services.data[0].id
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

    it(`Should get total number of unique service names accurately`, () => {
        const uniqueServiceNames = getUniqueResourceNamesStub(expectedTableRows, 'serviceIds')
        expect(wrapper.vm.$data.servicesLength).to.be.equals(uniqueServiceNames.length)
    })

    function getCell({ wrapper, rowId, cellId }) {
        const cellIndex = expectedTableHeaders.findIndex(item => item.value === cellId)
        const dataTable = wrapper.findComponent({ name: 'data-table' })
        const tableCell = dataTable.find(`.cell-${cellIndex}-${rowId}`)
        return tableCell
    }
    function getRepTooltipCell({ wrapper, rowId, cellId }) {
        const tableCell = getCell({ wrapper, rowId, cellId })
        const repTooltip = tableCell.findComponent({ name: 'rep-tooltip' })
        return repTooltip
    }
    function assertRepTooltipRequiredProps({ wrapper, rowId, cellId }) {
        const repTooltip = getRepTooltipCell({ wrapper, rowId, cellId })
        expect(repTooltip.exists()).to.be.true
        const { serverInfo } = repTooltip.vm.$props
        const { top } = repTooltip.vm.$attrs
        expect(serverInfo).to.be.deep.equals(
            expectedTableRows.find(row => row.id === rowId).serverInfo
        )
        expect(top).to.be.true
    }

    const cells = ['id', 'serverState']
    cells.forEach(cell => {
        describe(`Test ${cell} column - Show replication stats for server monitored
    by mariadbmon:`, () => {
            const testCases = ['master', 'slave']
            testCases.forEach(testCase => {
                let serverId
                let des = `${testCase} server: `
                switch (testCase) {
                    case 'master':
                        des = `Rendering rep-tooltip to show slave replication stats`
                        serverId = expectedTableRows[0].id
                        break
                    default:
                        serverId = expectedTableRows[1].id
                        des = `Rendering rep-tooltip to show replication stats`
                }
                it(des, () => {
                    const { isMaster } = getRepTooltipCell({
                        wrapper,
                        rowId: serverId,
                        cellId: cell,
                    }).vm.$props
                    switch (testCase) {
                        case 'master':
                            expect(isMaster).to.be.equals(true)
                            break
                        default:
                            expect(isMaster).to.be.equals(undefined)
                    }
                })
                it(`Assert required tooltip for ${testCase} server`, () => {
                    assertRepTooltipRequiredProps({ wrapper, rowId: serverId, cellId: cell })
                })
                it(`Assert add openDelay at server name cell for ${testCase} server`, () => {
                    const { 'open-delay': openDelay } = getRepTooltipCell({
                        wrapper,
                        rowId: serverId,
                        cellId: cell,
                    }).vm.$attrs
                    if (cell === 'id') expect(openDelay).to.be.equals(400)
                })
                it(`Add accurate classes to server cell slot for ${testCase} server`, () => {
                    const cellComponent = getCell({ wrapper, rowId: serverId, cellId: cell })
                    expect(cellComponent.find('.override-td--padding').exists()).to.be.true
                    // only add disable-auto-truncate class for server name cell
                    expect(cellComponent.find('.disable-auto-truncate').exists()).to.be[
                        cell === 'id'
                    ]
                })
            })
        })
    })

    it('Show not render rep-tooltip for server not monitored by mariadbmon', () => {
        const serverId = expectedTableRows[2].id
        const repTooltip = getRepTooltipCell({ wrapper, rowId: serverId, cellId: 'id' })
        expect(repTooltip.exists()).to.be.false
    })
})
