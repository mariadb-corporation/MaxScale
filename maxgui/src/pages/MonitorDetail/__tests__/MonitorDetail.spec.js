/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import store from 'store'

import mount, { router } from '@tests/unit/setup'
import MonitorDetail from '@/pages/MonitorDetail'

import {
    dummy_all_monitors,
    all_modules_map_stub,
    dummy_all_servers,
    getUnMonitoredServersStub,
    testRelationshipUpdate,
} from '@tests/unit/utils'

const allMonitorModules = all_modules_map_stub['Monitor']
const dummy_module_parameters = allMonitorModules.find(
    item => item.id === dummy_all_monitors[0].attributes.module
)

const defaultComputed = {
    search_keyword: () => '',
    overlay_type: () => null,
    module_parameters: () => dummy_module_parameters,
    current_monitor: () => dummy_all_monitors[0],
    all_servers: () => dummy_all_servers,
}

const {
    id: monitorId,
    attributes: { module: monitorModule },
} = defaultComputed.current_monitor()

const computedFactory = (computed = defaultComputed) =>
    mount({
        shallow: false,
        component: MonitorDetail,
        computed,
    })

describe('MonitorDetail index', () => {
    let wrapper, axiosGetStub, axiosPatchStub

    before(async () => {
        const monitorPath = `/dashboard/monitors/${dummy_all_monitors[0].id}`
        if (router.history.current.path !== monitorPath) await router.push(monitorPath)
        axiosGetStub = sinon.stub(store.$http, 'get').returns(
            Promise.resolve({
                data: {},
            })
        )
        wrapper = computedFactory()
        axiosPatchStub = sinon.stub(store.$http, 'patch').returns(Promise.resolve())
    })

    after(async () => {
        await axiosGetStub.restore()
        await axiosPatchStub.restore()
        await wrapper.destroy()
    })

    it(`Should send request to get monitor, relationships servers state
      and monitor module parameters`, async () => {
        await wrapper.vm.$nextTick(async () => {
            let {
                id,
                attributes: { module: moduleId },
                relationships: {
                    servers: { data: serversData },
                },
            } = dummy_all_monitors[0]

            await axiosGetStub.should.have.been.calledWith(`/monitors/${id}`)
            await axiosGetStub.should.have.been.calledWith(
                `/maxscale/modules/${moduleId}?fields[module]=parameters`
            )
            let count = 2
            await serversData.forEach(async server => {
                await axiosGetStub.should.have.been.calledWith(
                    `/servers/${server.id}?fields[servers]=state`
                )
                ++count
            })

            axiosGetStub.should.have.callCount(count)
        })
    })

    it(`Should get unmonitored servers as expected`, async () => {
        const unMonitoredServersStub = getUnMonitoredServersStub()
        // mockup changes of all_servers
        await wrapper.vm.$options.watch.all_servers.call(wrapper.vm)
        const unMonitoredServers = wrapper.vm.$data.unmonitoredServers

        expect(unMonitoredServers.length).to.be.equals(unMonitoredServersStub.length)

        unMonitoredServers.forEach((server, i) => {
            expect(server.id).to.be.equals(unMonitoredServersStub[i].id)
            expect(server.type).to.be.equals(unMonitoredServersStub[i].type)

            expect(server.state).to.be.not.undefined
        })
    })

    it(`Should send PATCH request with accurate payload to
      update server relationship`, async () => {
        const serverTableRowProcessingSpy = sinon.spy(wrapper.vm, 'serverTableRowProcessing')
        const relationshipType = 'servers'
        await testRelationshipUpdate({
            wrapper,
            currentResource: dummy_all_monitors[0],
            axiosPatchStub,
            relationshipType,
        })
        // callbacks after update monitor, re-fetching monitor
        await axiosGetStub.should.have.been.calledWith(`/monitors/${dummy_all_monitors[0].id}`)
        await serverTableRowProcessingSpy.should.have.been.calledOnce
    })

    describe('Module command switch-over assertions', () => {
        let switchoverSpy, overviewHeader, axiosPostStub
        const dummyMasterId = 'new_master_id'
        beforeEach(async () => {
            switchoverSpy = sinon.spy(MonitorDetail.methods, 'switchOver')

            wrapper = computedFactory()
            axiosPostStub = sinon.stub(store.$http, 'post').returns(
                Promise.resolve({
                    status: 204,
                })
            )
            // mock switch-over event emitted
            overviewHeader = wrapper.findComponent({ name: 'overview-header' })
            await overviewHeader.vm.$emit('switch-over', dummyMasterId)
        })

        afterEach(async () => {
            await axiosPostStub.restore()
            await switchoverSpy.restore()
            await wrapper.destroy()
        })

        it(`Should call switchOver action when @switch-over is emitted`, () => {
            switchoverSpy.should.have.been.calledOnce
        })

        it(`Should send POST request with accurate params to peform switchover`, async () => {
            await axiosPostStub.firstCall.should.have.been.calledWith(
                `/maxscale/modules/${monitorModule}/async-switchover?${monitorId}&${dummyMasterId}`
            )
        })
    })

    describe('Module command fetch-cmd-results assertions', () => {
        let fetchAsyncResultsSpy
        beforeEach(() => {
            fetchAsyncResultsSpy = sinon.spy(MonitorDetail.methods, 'fetchAsyncResults')
            wrapper = computedFactory()
        })

        afterEach(async () => {
            await fetchAsyncResultsSpy.restore()
            await wrapper.destroy()
        })

        it(`Should call fetch-cmd-results after calling async-switchover `, async () => {
            // mock calling switchOverCb
            await wrapper.vm.switchOverCb()
            fetchAsyncResultsSpy.should.have.been.calledOnce
            await axiosGetStub.should.have.been.calledWith(
                `/maxscale/modules/${monitorModule}/fetch-cmd-results?${monitorId}`
            )
        })
    })
})
