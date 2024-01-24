/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount, { router } from '@tests/unit/setup'
import ServiceDetail from '@rootSrc/pages/ServiceDetail'
import {
    dummy_all_services,
    dummy_all_sessions,
    testRelationshipUpdate,
    dummy_service_connection_datasets,
    dummy_service_connection_info,
} from '@tests/unit/utils'
import { MXS_OBJ_TYPES } from '@share/constants'

const routerDiagnosticsResStub = {
    queries: 0,
    replayed_transactions: 0,
    ro_transactions: 0,
    route_all: 0,
    route_master: 0,
    route_slave: 0,
    rw_transactions: 0,
    server_query_statistics: [],
}

const toServicePage = async () => {
    const servicePath = `/dashboard/services/${dummy_all_services[0].id}`
    if (router.history.current.path !== servicePath) await router.push(servicePath)
}

const defaultComputed = {
    current_service: () => dummy_all_services[0], // id: row_server_0
    service_connections_datasets: () => dummy_service_connection_datasets,
    serviceConnectionInfo: () => dummy_service_connection_info,
    filtered_sessions: () =>
        dummy_all_sessions.map(s => ({
            ...s,
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
        })),
    routerDiagnostics: () => routerDiagnosticsResStub,
}
const shallowMountOptions = {
    shallow: true,
    component: ServiceDetail,
    computed: defaultComputed,
    stubs: {
        'refresh-rate': "<div class='refresh-rate'></div>",
    },
}

describe('ServiceDetail index', () => {
    let wrapper, axiosGetStub, axiosPatchStub

    before(async () => {
        await toServicePage()
        wrapper = mount(shallowMountOptions)
        axiosGetStub = sinon.stub(wrapper.vm.$http, 'get').returns(Promise.resolve({ data: {} }))
        axiosPatchStub = sinon.stub(wrapper.vm.$http, 'patch').returns(Promise.resolve())
    })

    after(async () => {
        await axiosGetStub.restore()
        await axiosPatchStub.restore()
        await wrapper.destroy()
    })

    it(`Should send request to get current service, then requests to get service
      connections, sessions and diagnostics created by this service. After that fetch parallelly
      relationships type filters and listener state.`, async () => {
        await wrapper.vm.$nextTick(async () => {
            let {
                id,
                relationships: {
                    filters: { data: filtersData },
                    listeners: { data: listenersData },
                },
            } = dummy_all_services[0]

            await axiosGetStub.should.have.been.calledWith(`/services/${id}`)
            // sessions
            await axiosGetStub.should.have.been.calledWith(
                `/sessions?filter=/relationships/services/data/0/id="${id}"`
            )
            // diagnostics
            await axiosGetStub.should.have.been.calledWith(
                `/services/${id}?fields[services]=router_diagnostics`
            )
            let count = 4

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
            `/maxscale/modules/${router}?fields[modules]=parameters`
        )
    })

    describe('Relationships update test assertions', () => {
        const TYPES = ['filters']
        TYPES.forEach(type => {
            let des = 'Should send PATCH request with accurate payload to update'
            it(`${des} ${type} relationship`, async () => {
                wrapper = mount(shallowMountOptions)
                await testRelationshipUpdate({
                    wrapper,
                    currentResource: dummy_all_services[0],
                    axiosPatchStub,
                    relationshipType: type,
                })
                await axiosGetStub.should.have.been.calledWith(
                    `/services/${dummy_all_services[0].id}`
                )
            })
        })
    })

    describe('Props passes to child components test assertions', () => {
        before(() => {
            wrapper = mount(shallowMountOptions)
            sinon.stub(wrapper.vm, 'fetchAll').returns(
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
            const sessionsTable = wrapper.findComponent({ name: 'sessions-table' })
            expect(sessionsTable.exists()).to.be.true
            const { items } = sessionsTable.vm.$attrs
            const { collapsible, delayLoading } = sessionsTable.vm.$props
            expect(items).to.be.eql(wrapper.vm.sessionsTableRows)
            expect(collapsible).to.be.true
            expect(delayLoading).to.be.true
        })

        it(`Should compute sessions for this service with accurate data format`, () => {
            expect(wrapper.vm.sessionsTableRows[0]).to.include.all.keys(
                'id',
                'user',
                'connected',
                'idle',
                'memory'
            )
            expect(wrapper.vm.sessionsTableRows[0].memory).to.be.an('object')
        })

        it(`Should pass necessary props value to 'STATISTICS' table`, async () => {
            await wrapper.setData({ currentActiveTab: 1 })
            const statsTable = wrapper.findComponent({ ref: 'stats-table' })
            expect(statsTable.exists()).to.be.true
            const { title, tableData } = statsTable.vm.$props
            expect(title).to.be.equals('statistics')
            expect(tableData).to.be.deep.equals(wrapper.vm.serviceStats)
        })

        it(`Should pass necessary props to 'ROUTER DIAGNOSTICS' table`, async () => {
            await wrapper.setData({ currentActiveTab: 1 })
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

        it(`Should compute router diagnostics with accurate data format`, () => {
            expect(wrapper.vm.routerDiagnostics).to.be.deep.equals(routerDiagnosticsResStub)
        })

        it(`Should pass necessary props to details-parameters-table`, () => {
            const paramsTable = wrapper.findComponent({
                name: 'details-parameters-table',
            })
            expect(paramsTable.exists()).to.be.true
            const {
                resourceId,
                parameters: parametersProps,
                moduleParameters,
                updateResourceParameters,
                onEditSucceeded,
            } = paramsTable.vm.$props
            const {
                current_service: {
                    id,
                    attributes: { parameters },
                },
                module_parameters,
                updateServiceParameters,
                fetchService,
            } = wrapper.vm

            expect(resourceId).to.be.equals(id)
            expect(parametersProps).to.be.deep.equals(parameters)
            expect(moduleParameters).to.eql(module_parameters)
            expect(updateResourceParameters).to.be.equals(updateServiceParameters)
            expect(onEditSucceeded).to.be.equals(fetchService)
        })
    })

    describe('Test assertions for relationship-table', () => {
        let dispatchRelationshipUpdateSpy, SET_FORM_TYPE_STUB
        const ALL_RELATIONSHIP_TABLES = ['filters', 'listeners']

        beforeEach(() => {
            wrapper = mount(shallowMountOptions)
            sinon.stub(wrapper.vm, 'fetchAll').returns(
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
                        getRelationshipData,
                        addable,
                        removable,
                    } = relationshipTable.vm.$props
                    const {
                        getResourceData,
                        $data: { filtersTableRows, listenersTableRows },
                    } = wrapper.vm

                    expect(relationshipTable.exists()).to.be.true
                    expect(relationshipType).to.be.equals(name)

                    if (name === 'listeners') {
                        expect(getRelationshipData).to.be.undefined
                        expect(addable).to.be.true
                        expect(removable).to.be.false
                    } else {
                        expect(getRelationshipData).to.eql(getResourceData)
                        expect(addable).to.be.true
                        expect(removable).to.be.true
                    }
                    switch (name) {
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
                let des = `Should only call dispatchRelationshipUpdate for ${refName}`
                if (name === 'listeners') {
                    des = des.replace(
                        'dispatchRelationshipUpdate',
                        `SET_FORM_TYPE mutation with Listener as argument`
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
                        await SET_FORM_TYPE_STUB.should.have.been.calledWith(
                            MXS_OBJ_TYPES.LISTENERS
                        )
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
                        .stub(wrapper.vm, 'getResourceData')
                        .returns(Promise.resolve(dummy_resource))

                    await wrapper.vm.processRelationshipTable(type)
                    expect(wrapper.vm.$data[`${type}TableRows`]).to.be.deep.equals(tableRowStub)
                })
            })
        })
    })
})
