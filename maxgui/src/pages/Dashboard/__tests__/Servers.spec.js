/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/* eslint-disable no-unused-vars */
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import Servers from '@/pages/Dashboard/Servers'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import {
    getAllMonitorsMapStub,
    findAnchorLinkInTable,
    getUniqueResourceNamesStub,
} from '@tests/unit/utils'

chai.should()
chai.use(sinonChai)

const all_servers_mockup = [
    {
        id: 'row_server_0',
        attributes: {
            state: 'Master, Running',
            statistics: { connections: 100 },
            parameters: {
                address: '127.0.0.1',
                port: 4000,
            },
            gtid_current_pos: '0-1000-9',
        },
        relationships: {
            monitors: {
                data: [
                    {
                        id: 'monitor_0',
                        type: 'monitors',
                    },
                ],
            },
            services: {
                data: [
                    {
                        id: 'service_0',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'servers',
    },
    {
        id: 'row_server_1',
        attributes: {
            state: 'Slave, Running',
            statistics: { connections: 1000 },
            parameters: {
                address: '127.0.0.1',
                port: 4001,
            },
            gtid_current_pos: '0-1000-9',
        },
        relationships: {
            monitors: {
                data: [
                    {
                        id: 'monitor_0',
                        type: 'monitors',
                    },
                ],
            },
        },
        type: 'servers',
    },
]
const expectedTableHeaders = [
    { text: `Monitor`, value: 'groupId' },
    { text: 'State', value: 'monitorState' },
    { text: 'Servers', value: 'id' },
    { text: 'Address', value: 'serverAddress' },
    { text: 'Port', value: 'serverPort' },
    { text: 'Connections', value: 'serverConnections' },
    { text: 'State', value: 'serverState' },
    { text: 'GTID', value: 'gtid' },
    { text: 'Services', value: 'serviceIds' },
]

const expectedTableRows = [
    {
        id: 'row_server_0',
        serverAddress: '127.0.0.1',
        serverPort: 4000,
        serverConnections: 100,
        serverState: 'Master, Running',
        serviceIds: ['service_0'],
        gtid: '0-1000-9',
        groupId: 'monitor_0',
        monitorState: 'Running',
    },
    {
        id: 'row_server_1',
        serverAddress: '127.0.0.1',
        serverPort: 4001,
        serverConnections: 1000,
        serverState: 'Slave, Running',
        serviceIds: 'No services',
        gtid: '0-1000-9',
        groupId: 'monitor_0',
        monitorState: 'Running',
    },
]

describe('Dashboard Servers tab', () => {
    let wrapper, axiosStub

    after(async () => {
        await axiosStub.reset()
    })

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: Servers,
            computed: {
                all_servers: () => all_servers_mockup,
                getAllMonitorsMap: () => getAllMonitorsMapStub,
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$axios, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
    })

    afterEach(async function() {
        await axiosStub.restore()
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
        const serverId = all_servers_mockup[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: serverId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'id'),
        })
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/servers/${serverId}`)
    })

    it(`Should navigate to service detail page when a service is clicked`, async () => {
        const serverId = all_servers_mockup[0].id
        const serviceId = all_servers_mockup[0].relationships.services.data[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: serverId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'serviceIds'),
        })
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
    })

    it(`Should navigate to monitor detail page when a monitor is clicked`, async () => {
        const serverId = all_servers_mockup[0].id
        const monitorId = all_servers_mockup[0].relationships.monitors.data[0].id
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
