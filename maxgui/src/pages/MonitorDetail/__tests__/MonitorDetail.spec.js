/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import chai, { expect } from 'chai'
import mount, { router } from '@tests/unit/setup'
import MonitorDetail from '@/pages/MonitorDetail'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import {
    dummy_all_monitors,
    all_modules_map_stub,
    dummy_all_servers,
    getUnMonitoredServersStub,
    testRelationshipUpdate,
} from '@tests/unit/utils'
chai.should()
chai.use(sinonChai)

describe('MonitorDetail index', () => {
    let wrapper, axiosGetStub, axiosPatchStub
    const allMonitorModules = all_modules_map_stub['Monitor']
    const dummy_module_parameters = allMonitorModules.find(
        item => item.id === dummy_all_monitors[0].attributes.module
    )
    before(async () => {
        const monitorPath = `/dashboard/monitors/${dummy_all_monitors[0].id}`
        if (router.history.current.path !== monitorPath) await router.push(monitorPath)

        axiosGetStub = sinon.stub(Vue.prototype.$axios, 'get').returns(
            Promise.resolve({
                data: {},
            })
        )

        wrapper = mount({
            shallow: false,
            component: MonitorDetail,
            computed: {
                search_keyword: () => '',
                overlay_type: () => null,
                module_parameters: () => dummy_module_parameters,
                current_monitor: () => dummy_all_monitors[0],
                all_servers: () => dummy_all_servers,
            },
        })

        axiosPatchStub = sinon.stub(wrapper.vm.$axios, 'patch').returns(Promise.resolve())
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
})
