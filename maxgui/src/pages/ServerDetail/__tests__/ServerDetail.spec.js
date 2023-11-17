/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount, { router } from '@tests/unit/setup'
import ServerDetail from '@rootSrc/pages/ServerDetail'
import { dummy_all_servers, dummy_all_sessions } from '@tests/unit/utils'
import { lodash } from '@share/utils/helpers'

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

const toServerPage = async () => {
    const serverPath = `/dashboard/servers/${dummy_all_servers[0].id}`
    if (router.history.current.path !== serverPath) await router.push(serverPath)
}

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: ServerDetail,
                computed: {
                    current_server: () => dummy_all_servers[0], // id: row_server_0
                    monitor_diagnostics: () => dummy_monitor_diagnostics,
                    filtered_sessions: () => dummy_all_sessions,
                },
                stubs: {
                    'refresh-rate': "<div class='refresh-rate'></div>",
                },
            },
            opts
        )
    )
let wrapper
describe('ServerDetail index', () => {
    before(() => toServerPage())
    describe('Statistics & Sessions tab', () => {
        before(() => {
            wrapper = mountFactory()
        })
        it(`Should pass necessary props value to 'STATISTICS' table`, () => {
            const statsTable = wrapper.findAllComponents({ name: 'details-readonly-table' }).at(0)
            expect(statsTable.exists()).to.be.true
            const { title, tableData, isTree } = statsTable.vm.$props
            expect(title).to.be.equals('statistics')
            expect(tableData).to.be.deep.equals(wrapper.vm.serverStats)
            expect(isTree).to.be.true
        })
        it(`Should pass necessary props value to 'CURRENT SESSIONS' table`, () => {
            const sessionsTable = wrapper.findComponent({ name: 'sessions-table' })
            expect(sessionsTable.exists()).to.be.true
            const { items } = sessionsTable.vm.$attrs
            const { collapsible, delayLoading } = sessionsTable.vm.$props
            const { sessionsTableRow } = wrapper.vm

            expect(items).to.be.eql(sessionsTableRow)
            expect(collapsible).to.be.true
            expect(delayLoading).to.be.true
        })
        it(`Should pass necessary props value to 'SERVICES' table`, () => {
            const servicesTable = wrapper.findComponent({ name: 'relationship-table' })
            expect(servicesTable.exists()).to.be.true
            const {
                relationshipType,
                addable,
                removable,
                tableRows,
                getRelationshipData,
            } = servicesTable.vm.$props

            const {
                $data: { serviceTableRow },
                getResourceData,
            } = wrapper.vm

            expect(relationshipType).to.be.equals('services')
            expect(addable).to.be.true
            expect(removable).to.be.true
            expect(tableRows).to.be.deep.equals(serviceTableRow)
            expect(getRelationshipData).to.be.equals(getResourceData)
        })

        it(`Should pass necessary props res-time-dist-histogram`, () => {
            const { resTimeDist } = wrapper.findComponent({
                name: 'res-time-dist-histogram',
            }).vm.$props
            expect(resTimeDist).to.eql(wrapper.vm.resTimeDist)
        })
    })
    describe("Parameters & Diagnostics tab - Child component's data communication tests", () => {
        beforeEach(() => {
            wrapper = mountFactory({ data: () => ({ currentActiveTab: 1 }) })
        })
        it(`Should pass necessary props value to 'MONITOR DIAGNOSTICS' table`, () => {
            const diagnosticsTable = wrapper
                .findAllComponents({ name: 'details-readonly-table' })
                .at(0)
            const { title, tableData, isTree, expandAll } = diagnosticsTable.vm.$props
            expect(title).to.be.equals('Monitor Diagnostics')
            expect(isTree).to.be.true
            expect(expandAll).to.be.true
            expect(tableData).to.be.deep.equals(wrapper.vm.monitorDiagnostics)
        })
        it(`Should pass necessary props value to 'PARAMETERS' table`, () => {
            const paramsTable = wrapper.findComponent({ name: 'details-parameters-table' })
            expect(paramsTable.exists()).to.be.true
            const {
                resourceId,
                parameters,
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
            expect(updateResourceParameters).to.be.equals(updateServerParameters)
            expect(onEditSucceeded).to.be.equals(dispatchFetchServer)
        })
    })
    describe('Computed tests', () => {
        it(`Should compute sessions for this server to accurate data format`, () => {
            expect(wrapper.vm.sessionsTableRow[0]).to.include.all.keys(
                'id',
                'user',
                'connected',
                'idle',
                'memory'
            )
            expect(wrapper.vm.sessionsTableRow[0].memory).to.be.an('object')
        })
        it(`Should compute monitor diagnostics for this server to accurate data format`, () => {
            expect(wrapper.vm.monitorDiagnostics).to.be.deep.equals(monitorDiagnosticsStub)
        })
        it(`Should compute serviceTableRow for this server to accurate data format`, async () => {
            let getResourceDataStub

            getResourceDataStub = sinon.stub(wrapper.vm, 'getResourceData')
            getResourceDataStub.onCall(0).returns(
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
    })
})
