/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import store from 'store'

import mount, { router } from '@tests/unit/setup'
import ServiceDetail from '@/pages/ServiceDetail'

import {
    dummy_all_services,
    testRelationshipUpdate,
    dummy_service_connection_datasets,
    dummy_service_connection_info,
} from '@tests/unit/utils'

const dummy_sessions_by_service = [
    {
        attributes: {
            connected: 'Thu Aug 27 15:05:28 2020',
            idle: 8.9,
            remote: '::ffff:127.0.0.1',
            user: 'maxskysql',
        },
        id: '100002',
        relationships: {
            services: {
                data: [
                    {
                        id: 'service_0',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'sessions',
    },
]

const sessionsTableRowStub = [
    {
        id: '100002',
        user: 'maxskysql@::ffff:127.0.0.1',
        connected: '15:05:28 08.27.2020',
        idle: 8.9,
    },
]

const EXPECT_SESSIONS_HEADER = [
    { text: 'ID', value: 'id' },
    { text: 'Client', value: 'user' },
    { text: 'Connected', value: 'connected' },
    { text: 'IDLE (s)', value: 'idle' },
]

const routerDiagnosticsResStub = {
    attributes: {
        router_diagnostics: {
            queries: 0,
            replayed_transactions: 0,
            ro_transactions: 0,
            route_all: 0,
            route_master: 0,
            route_slave: 0,
            rw_transactions: 0,
            server_query_statistics: [],
        },
    },
}

const toServicePage = async () => {
    const servicePath = `/dashboard/services/${dummy_all_services[0].id}`
    if (router.history.current.path !== servicePath) await router.push(servicePath)
}

const defaultComputed = {
    current_service: () => dummy_all_services[0], // id: row_server_0
    service_connections_datasets: () => dummy_service_connection_datasets,
    service_connection_info: () => dummy_service_connection_info,
    sessions_by_service: () => dummy_sessions_by_service,
    current_service_diagnostics: () => routerDiagnosticsResStub,
}
const shallowMountOptions = {
    shallow: true,
    component: ServiceDetail,
    computed: defaultComputed,
}

describe('ServiceDetail index', () => {
    let wrapper, axiosGetStub, axiosPatchStub

    before(async () => {
        await toServicePage()
        axiosGetStub = sinon.stub(store.$http, 'get').returns(
            Promise.resolve({
                data: {},
            })
        )
        axiosPatchStub = sinon.stub(store.$http, 'patch').returns(Promise.resolve())
        wrapper = mount(shallowMountOptions)
    })

    after(async () => {
        await axiosGetStub.restore()
        await axiosPatchStub.restore()
        await wrapper.destroy()
    })

    it(`Should send request to get current service, then requests to get service
      connections, sessions and diagnostics created by this service. After that fetch parallelly
      relationships type servers, filters and listener state.`, async () => {
        await wrapper.vm.$nextTick(async () => {
            let {
                id,

                relationships: {
                    servers: { data: serversData },
                    filters: { data: filtersData },
                    listeners: { data: listenersData },
                },
            } = dummy_all_services[0]

            await axiosGetStub.should.have.been.calledWith(`/services/${id}`)
            // connections
            await axiosGetStub.should.have.been.calledWith(
                `/services/${id}?fields[services]=connections,total_connections`
            )
            // sessions
            await axiosGetStub.should.have.been.calledWith(
                `/sessions?filter=/relationships/services/data/0/id="${id}"`
            )
            // diagnostics
            await axiosGetStub.should.have.been.calledWith(
                `/services/${id}?fields[services]=router_diagnostics`
            )
            let count = 4
            await serversData.forEach(async server => {
                await axiosGetStub.should.have.been.calledWith(
                    `/servers/${server.id}?fields[servers]=state`
                )
                ++count
            })
            await filtersData.forEach(async filter => {
                await axiosGetStub.should.have.been.calledWith(
                    `/filters/${filter.id}?fields[filters]=state`
                )
                ++count
            })
            await listenersData.forEach(async listener => {
                await axiosGetStub.should.have.been.calledWith(
                    `/listeners/${listener.id}?fields[listeners]=state`
                )
                ++count
            })

            axiosGetStub.should.have.callCount(count)
        })
    })

    it(`Should send GET requests to get router module parameters
      if current active tab is 'Parameters & Relationships tab'`, async () => {
        await wrapper.setData({
            currentActiveTab: 0,
        })
        const router = dummy_all_services[0].attributes.router
        await axiosGetStub.should.have.been.calledWith(
            `/maxscale/modules/${router}?fields[module]=parameters`
        )
    })

    describe('Relationships update test assertions', () => {
        const TYPES = ['servers', 'filters']
        TYPES.forEach(type => {
            let des = 'Should send PATCH request with accurate payload to update'
            it(`${des} ${type} relationship`, async () => {
                wrapper = mount(shallowMountOptions)
                const tableRowProcessingSpy = sinon.spy(wrapper.vm, 'processingRelationshipTable')
                await testRelationshipUpdate({
                    wrapper,
                    currentResource: dummy_all_services[0],
                    axiosPatchStub,
                    relationshipType: type,
                })
                await axiosGetStub.should.have.been.calledWith(
                    `/services/${dummy_all_services[0].id}`
                )
                // callback after update
                await tableRowProcessingSpy.should.have.been.calledOnce
            })
        })
    })

    describe('Props passes to child components test assertions', () => {
        before(() => {
            wrapper = mount(shallowMountOptions)
            sinon.stub(wrapper.vm, 'fetchConnSessDiag').returns(
                Promise.resolve({
                    data: {},
                })
            )
        })

        it(`Should pass necessary props to page-header`, () => {
            const pageHeader = wrapper.findComponent({ name: 'page-header' })
            expect(pageHeader.exists()).to.be.true
            const { currentService, onEditSucceeded } = pageHeader.vm.$props

            expect(currentService).to.be.deep.equals(dummy_all_services[0])
            expect(onEditSucceeded).to.be.equals(wrapper.vm.fetchService)
        })

        it(`Should pass necessary props to overview-header`, () => {
            const overviewHeader = wrapper.findComponent({ name: 'overview-header' })
            expect(overviewHeader.exists()).to.be.true
            const {
                currentService,
                serviceConnectionsDatasets,
                serviceConnectionInfo,
            } = overviewHeader.vm.$props
            expect(currentService).to.be.deep.equals(dummy_all_services[0])
            expect(serviceConnectionsDatasets).to.be.deep.equals(dummy_service_connection_datasets)
            expect(serviceConnectionInfo).to.be.deep.equals(dummy_service_connection_info)
        })

        it(`Should pass necessary props to 'CURRENT SESSIONS' table`, () => {
            const sessionsTable = wrapper.findComponent({
                ref: 'sessions-table',
            })
            expect(sessionsTable.exists()).to.be.true
            const {
                tdBorderLeft,
                title,
                titleInfo,
                noDataText,
                tableData,
                customTableHeaders,
            } = sessionsTable.vm.$props
            const {
                $data: { sessionsTableHeader },
                sessionsTableRows,
            } = wrapper.vm

            expect(tdBorderLeft).to.be.false
            expect(title).to.be.equals('current sessions')
            expect(titleInfo).to.be.equals(sessionsTableRows.length)
            expect(noDataText).to.be.equals('No sessions')
            expect(tableData).to.be.equals(sessionsTableRows)
            expect(customTableHeaders).to.be.equals(sessionsTableHeader)
        })

        it(`Should use accurate table headers for 'CURRENT SESSIONS' table`, () => {
            const sessionsTable = wrapper.findComponent({
                ref: 'sessions-table',
            })
            expect(sessionsTable.vm.$props.customTableHeaders).to.be.deep.equals(
                EXPECT_SESSIONS_HEADER
            )
        })

        it(`Should compute sessions for this service with accurate data format`, () => {
            expect(wrapper.vm.sessionsTableRows).to.be.deep.equals(sessionsTableRowStub)
        })

        it(`Should pass necessary props to 'ROUTER DIAGNOSTICS' table`, async () => {
            await wrapper.setData({
                currentActiveTab: 1,
            })
            await wrapper.vm.$nextTick(() => {
                const diagnosticsTable = wrapper.findComponent({
                    ref: 'diagnostics-table',
                })
                expect(diagnosticsTable.exists()).to.be.true
                const { title, tableData, isTree, expandAll } = diagnosticsTable.vm.$props
                const { routerDiagnostics } = wrapper.vm

                expect(title).to.be.equals('Router Diagnostics')
                expect(tableData).to.be.equals(routerDiagnostics)
                expect(isTree).to.be.true
                expect(expandAll).to.be.true
            })
        })

        it(`Should compute router diagnostics with accurate data format`, () => {
            expect(wrapper.vm.routerDiagnostics).to.be.deep.equals(
                routerDiagnosticsResStub.attributes.router_diagnostics
            )
        })

        it(`Should pass necessary props to details-parameters-table`, () => {
            const paramsTable = wrapper.findComponent({
                name: 'details-parameters-table',
            })
            expect(paramsTable.exists()).to.be.true
            const {
                resourceId,
                parameters: parametersProps,
                updateResourceParameters,
                onEditSucceeded,
            } = paramsTable.vm.$props
            const {
                current_service: {
                    id,
                    attributes: { parameters },
                },
                updateServiceParameters,
                fetchService,
            } = wrapper.vm

            expect(resourceId).to.be.equals(id)
            expect(parametersProps).to.be.deep.equals(parameters)
            expect(updateResourceParameters).to.be.equals(updateServiceParameters)
            expect(onEditSucceeded).to.be.equals(fetchService)
        })
    })

    describe('Test assertions for relationship-table', () => {
        let dispatchRelationshipUpdateSpy, SET_FORM_TYPE_STUB
        const ALL_RELATIONSHIP_TABLES = ['servers', 'filters', 'listeners']

        beforeEach(() => {
            wrapper = mount(shallowMountOptions)
            sinon.stub(wrapper.vm, 'fetchConnSessDiag').returns(
                Promise.resolve({
                    data: {},
                })
            )
            dispatchRelationshipUpdateSpy = sinon.spy(wrapper.vm, 'dispatchRelationshipUpdate')
            SET_FORM_TYPE_STUB = sinon.stub(wrapper.vm, 'SET_FORM_TYPE')
        })

        afterEach(() => {
            dispatchRelationshipUpdateSpy.restore()
            SET_FORM_TYPE_STUB.restore()
        })

        describe('Props passes to relationship-table test assertions', () => {
            ALL_RELATIONSHIP_TABLES.forEach(name => {
                it(`Should pass necessary props to ${name} relationship-table`, () => {
                    const relationshipTable = wrapper.findComponent({
                        ref: `${name}-relationship-table`,
                    })
                    const {
                        relationshipType,
                        tableRows,
                        getRelationshipData: getRelationshipDataProps,
                        readOnly,
                    } = relationshipTable.vm.$props
                    const {
                        getRelationshipData,
                        $data: { serversTableRows, filtersTableRows, listenersTableRows },
                    } = wrapper.vm

                    expect(relationshipTable.exists()).to.be.true
                    expect(relationshipType).to.be.equals(name)

                    if (name === 'listeners') {
                        expect(getRelationshipDataProps).to.be.undefined
                        expect(readOnly).to.be.true
                    } else {
                        expect(getRelationshipDataProps).to.be.equals(getRelationshipData)
                        expect(readOnly).to.be.false
                    }
                    switch (name) {
                        case 'servers':
                            expect(tableRows).to.be.deep.equals(serversTableRows)
                            break
                        case 'filters':
                            expect(tableRows).to.be.deep.equals(filtersTableRows)
                            break
                        case 'listeners':
                            expect(tableRows).to.be.deep.equals(listenersTableRows)
                    }
                })
            })
        })

        describe('Passes event handler to relationship-table test assertions', () => {
            ALL_RELATIONSHIP_TABLES.forEach(name => {
                const refName = `${name}-relationship-table`
                let des = `Should only call dispatchRelationshipUpdate method for ${refName}`
                if (name === 'listeners') {
                    des = des.replace(
                        'dispatchRelationshipUpdate',
                        'SET_FORM_TYPE mutation with FORM_LISTENER as argument'
                    )
                }

                it(des, async () => {
                    const relationshipTable = wrapper.findComponent({
                        ref: refName,
                    })

                    await relationshipTable.vm.$emit('on-relationship-update', {
                        type: name,
                        data: [],
                        isFilterDrag: name === 'filters',
                    })

                    await relationshipTable.vm.$emit('open-listener-form-dialog')

                    if (name === 'listeners') {
                        await SET_FORM_TYPE_STUB.should.have.been.called
                        await SET_FORM_TYPE_STUB.should.have.been.calledWith('FORM_LISTENER')
                        await dispatchRelationshipUpdateSpy.should.have.not.been.called
                    } else {
                        await SET_FORM_TYPE_STUB.should.have.not.been.called
                        await dispatchRelationshipUpdateSpy.should.have.been.calledOnce
                    }
                })
            })
        })

        describe('Should process data accurately for relationship-table', () => {
            ALL_RELATIONSHIP_TABLES.forEach(type => {
                const refName = `${type}-relationship-table`
                it(`Should process ${type}TableRows for ${refName}`, async () => {
                    let tableRowStub = []
                    let dummy_resource = {}
                    switch (type) {
                        case 'servers':
                            dummy_resource = {
                                attributes: {
                                    state: 'Master, Running',
                                },
                                id: 'row_server_0',
                                type,
                            }
                            tableRowStub = [
                                {
                                    id: 'row_server_0',
                                    state: 'Master, Running',
                                    type,
                                },
                            ]
                            break
                        case 'filters':
                            dummy_resource = {
                                id: 'filter_0',
                                type,
                            }
                            tableRowStub = [
                                {
                                    id: 'filter_0',
                                    type,
                                },
                            ]
                            break
                        case 'listeners':
                            dummy_resource = {
                                attributes: {
                                    state: 'Running',
                                },
                                id: 'RCR-Router-Listener',
                                type,
                            }
                            tableRowStub = [
                                {
                                    id: 'RCR-Router-Listener',
                                    state: 'Running',
                                    type,
                                },
                            ]
                    }
                    sinon
                        .stub(wrapper.vm, 'getRelationshipData')
                        .returns(Promise.resolve(dummy_resource))

                    await wrapper.vm.processingRelationshipTable(type)
                    expect(wrapper.vm.$data[`${type}TableRows`]).to.be.deep.equals(tableRowStub)
                })
            })
        })
    })
})
