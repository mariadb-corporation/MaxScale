/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import chai, { expect } from 'chai'
import mount, { router } from '@tests/unit/setup'
import ServerDetail from '@/pages/ServerDetail'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { dummy_all_servers, testRelationshipUpdate } from '@tests/unit/utils'
chai.should()
chai.use(sinonChai)

const dummy_server_stats = {
    active_operations: 0,
    adaptive_avg_select_time: '0ns',
    connections: 0,
    max_connections: 0,
    persistent_connections: 0,
    routed_packets: 0,
    total_connections: 0,
}

const dummy_monitor_diagnostics = {
    attributes: {
        monitor_diagnostics: {
            master: 'row_server_0',
            master_gtid_domain_id: 0,
            primary: null,
            server_info: [
                {
                    gtid_binlog_pos: '0-1000-9',
                    gtid_current_pos: '0-1000-9',
                    lock_held: null,
                    master_group: null,
                    name: 'row_server_0',
                    read_only: false,
                    server_id: 1000,
                    slave_connections: [],
                },
                {
                    gtid_binlog_pos: '0-1000-9',
                    gtid_current_pos: '0-1000-9',
                    lock_held: null,
                    master_group: null,
                    name: 'row_server_1',
                    read_only: false,
                    server_id: 1001,
                    slave_connections: [
                        {
                            connection_name: '',
                            gtid_io_pos: '0-1000-9',
                            last_io_error: '',
                            last_sql_error: '',
                            master_host: '127.0.0.1',
                            master_port: 4001,
                            master_server_id: 1000,
                            seconds_behind_master: 0,
                            slave_io_running: 'Yes',
                            slave_sql_running: 'Yes',
                        },
                    ],
                },
            ],
            state: 'Idle',
        },
    },
    id: 'Monitor',
    type: 'monitors',
}

const monitorDiagnosticsStub = {
    gtid_binlog_pos: '0-1000-9',
    gtid_current_pos: '0-1000-9',
    lock_held: null,
    master_group: null,
    name: 'row_server_0',
    read_only: false,
    server_id: 1000,
    slave_connections: [],
}

const dummy_all_sessions = [
    {
        attributes: {
            connected: 'Thu Aug 27 09:13:53 2020',
            connections: [
                {
                    connection_id: 14,
                    server: 'row_server_0',
                },
                {
                    connection_id: 13,
                    server: 'row_server_1',
                },
            ],
            idle: 27.800000000000001,
            log: [],
            queries: [],
            remote: '::ffff:127.0.0.1',
            state: 'Session started',
            user: 'maxskysql',
        },
        id: '1',
        type: 'sessions',
    },
]

const sessionsTableRowStub = [
    {
        id: '1',
        user: 'maxskysql@::ffff:127.0.0.1',
        connected: '09:13:53 08.27.2020',
        idle: 27.8,
    },
]

const EXPECT_SESSIONS_HEADER = [
    { text: 'ID', value: 'id' },
    { text: 'Client', value: 'user' },
    { text: 'Connected', value: 'connected' },
    { text: 'IDLE (s)', value: 'idle' },
]

const toServerPage = async () => {
    const serverPath = `/dashboard/servers/${dummy_all_servers[0].id}`
    if (router.history.current.path !== serverPath) await router.push(serverPath)
}

const mountOptions = {
    shallow: false,
    component: ServerDetail,
    computed: {
        current_server: () => dummy_all_servers[0], // id: row_server_0
        current_server_stats: () => dummy_server_stats,
        monitor_diagnostics: () => dummy_monitor_diagnostics,
        all_sessions: () => dummy_all_sessions,
    },
}

describe('ServerDetail index', () => {
    let wrapper, axiosGetStub, axiosPatchStub

    before(async () => {
        await toServerPage()
        axiosGetStub = sinon.stub(Vue.prototype.$axios, 'get').returns(
            Promise.resolve({
                data: {},
            })
        )

        wrapper = mount(mountOptions)

        axiosPatchStub = sinon.stub(wrapper.vm.$axios, 'patch').returns(Promise.resolve())
    })

    after(async () => {
        await axiosGetStub.restore()
        await axiosPatchStub.restore()
        await wrapper.destroy()
    })

    it(`Should send request to get current server, relationships
      services state then server statistics and all sessions
      if current active tab is 'Statistics & Sessions'`, async () => {
        await wrapper.vm.$nextTick(async () => {
            let {
                id,

                relationships: {
                    services: { data: servicesData },
                },
            } = dummy_all_servers[0]

            await axiosGetStub.should.have.been.calledWith(`/servers/${id}`)
            let count = 1
            await servicesData.forEach(async service => {
                await axiosGetStub.should.have.been.calledWith(
                    `/services/${service.id}?fields[services]=state`
                )
                ++count
            })

            await axiosGetStub.should.have.been.calledWith(
                `/servers/${id}?fields[servers]=statistics`
            )
            ++count
            await axiosGetStub.should.have.been.calledWith(`/sessions`)
            ++count
            axiosGetStub.should.have.callCount(count)
        })
    })

    it(`Should send GET requests to get server module parameters and
       monitor diagnostics if current active tab is 'Parameters & Diagnostics tab'`, async () => {
        await wrapper.setData({
            currentActiveTab: 1,
        })
        const monitorId = dummy_all_servers[0].relationships.monitors.data[0].id
        await axiosGetStub.should.have.been.calledWith(
            `/monitors/${monitorId}?fields[monitors]=monitor_diagnostics`
        )
        await axiosGetStub.should.have.been.calledWith(
            `/maxscale/modules/servers?fields[module]=parameters`
        )
    })

    it(`Should send PATCH request with accurate payload to
      update services relationship`, async () => {
        const serviceTableRowProcessingSpy = sinon.spy(wrapper.vm, 'serviceTableRowProcessing')
        const relationshipType = 'services'
        await testRelationshipUpdate({
            wrapper,
            currentResource: dummy_all_servers[0],
            axiosPatchStub,
            relationshipType,
        })
        await axiosGetStub.should.have.been.calledWith(`/servers/${dummy_all_servers[0].id}`)
        // callback after update
        await serviceTableRowProcessingSpy.should.have.been.calledOnce
    })

    it(`Should send PATCH request with accurate payload to
      update monitors relationship`, async () => {
        const fetchMonitorDiagnosticsSpy = sinon.spy(wrapper.vm, 'fetchMonitorDiagnostics')
        const relationshipType = 'monitors'
        await testRelationshipUpdate({
            wrapper,
            currentResource: dummy_all_servers[0],
            axiosPatchStub,
            relationshipType,
        })
        await axiosGetStub.should.have.been.calledWith(`/servers/${dummy_all_servers[0].id}`)
        // callback after update
        await fetchMonitorDiagnosticsSpy.should.have.been.calledOnce
    })

    it(`Should pass necessary props value to 'STATISTICS' table`, async () => {
        const statsTable = wrapper.findComponent({ ref: 'statistics-table' })
        expect(statsTable.exists()).to.be.true
        const { title, tableData } = statsTable.vm.$props
        expect(title).to.be.equals('statistics')
        expect(tableData).to.be.deep.equals(dummy_server_stats)
    })

    it(`Should pass necessary props value to 'CURRENT SESSIONS' table`, async () => {
        const sessionsTable = wrapper.findComponent({ ref: 'sessions-table' })
        expect(sessionsTable.exists()).to.be.true
        const {
            title,
            titleInfo,
            tdBorderLeft,
            noDataText,
            tableData,
            customTableHeaders,
        } = sessionsTable.vm.$props

        const { sessionsTableRow, sessionsTableHeader } = wrapper.vm

        expect(title).to.be.equals('current sessions')
        expect(titleInfo).to.be.equals(sessionsTableRow.length)
        expect(tdBorderLeft).to.be.false
        expect(noDataText).to.be.equals('No sessions')
        expect(tableData).to.be.deep.equals(sessionsTableRow)
        expect(customTableHeaders).to.be.deep.equals(sessionsTableHeader)
    })

    it(`Should use accurate table headers for 'CURRENT SESSIONS' table`, async () => {
        const sessionsTable = wrapper.findComponent({ ref: 'sessions-table' })
        expect(sessionsTable.vm.$props.customTableHeaders).to.be.deep.equals(EXPECT_SESSIONS_HEADER)
    })

    it(`Should compute sessions for this server to accurate data format`, async () => {
        expect(wrapper.vm.sessionsTableRow).to.be.deep.equals(sessionsTableRowStub)
    })

    it(`Should pass necessary props value to 'MONITOR DIAGNOSTICS' table`, async () => {
        const diagnosticsTable = wrapper.findComponent({ ref: 'diagnostics-table' })
        expect(diagnosticsTable.exists()).to.be.true
        const { title, tableData, isTree } = diagnosticsTable.vm.$props

        expect(title).to.be.equals('Monitor Diagnostics')
        expect(isTree).to.be.true
        expect(tableData).to.be.deep.equals(wrapper.vm.monitorDiagnostics)
    })

    it(`Should compute monitor diagnostics for this server to accurate data format`, async () => {
        expect(wrapper.vm.monitorDiagnostics).to.be.deep.equals(monitorDiagnosticsStub)
    })

    it(`Should pass necessary props value to 'SERVICES' table`, async () => {
        const servicesTable = wrapper.findComponent({ name: 'relationship-table' })
        expect(servicesTable.exists()).to.be.true
        const { relationshipType, tableRows, getRelationshipData } = servicesTable.vm.$props

        const {
            $data: { serviceTableRow },
            getRelationshipData: getRelationshipDataAsync,
        } = wrapper.vm

        expect(relationshipType).to.be.equals('services')
        expect(tableRows).to.be.deep.equals(serviceTableRow)
        expect(getRelationshipData).to.be.equals(getRelationshipDataAsync)
    })

    it(`Should compute serviceTableRow for this server to accurate data format`, async () => {
        let getRelationshipDataStub

        getRelationshipDataStub = sinon.stub(wrapper.vm, 'getRelationshipData')
        getRelationshipDataStub.onCall(0).returns(
            Promise.resolve({
                attributes: {
                    state: 'Started',
                },
                id: 'service_0',
                type: 'services',
            })
        )

        const serviceTableRowStub = [
            {
                id: 'service_0',
                state: 'Started',
                type: 'services',
            },
        ]
        await wrapper.vm.serviceTableRowProcessing()
        expect(wrapper.vm.$data.serviceTableRow).to.be.deep.equals(serviceTableRowStub)
    })

    it(`Should pass necessary props value to 'PARAMETERS' table`, async () => {
        const paramsTable = wrapper.findComponent({ name: 'details-parameters-table' })
        expect(paramsTable.exists()).to.be.true
        const {
            resourceId,
            parameters,
            usePortOrSocket,
            updateResourceParameters,
            onEditSucceeded,
        } = paramsTable.vm.$props

        const { updateServerParameters, dispatchFetchServer } = wrapper.vm
        const {
            id: serverId,
            attributes: { parameters: serverParams },
        } = dummy_all_servers[0]

        expect(resourceId).to.be.equals(serverId)
        expect(parameters).to.be.deep.equals(serverParams)
        expect(usePortOrSocket).to.be.true
        expect(updateResourceParameters).to.be.equals(updateServerParameters)
        expect(onEditSucceeded).to.be.equals(dispatchFetchServer)
    })
})
