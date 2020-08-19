/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import chai from 'chai'
import mount, { router } from '@tests/unit/setup'
import MonitorDetail from '@/pages/MonitorDetail'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { dummy_all_monitors, all_modules_map_stub } from '@tests/unit/utils'
chai.should()
chai.use(sinonChai)

describe('MonitorDetail index', () => {
    let wrapper, axiosStub
    const allMonitorModules = all_modules_map_stub['Monitor']
    const dummy_module_parameters = allMonitorModules.find(
        item => item.id === dummy_all_monitors[0].attributes.module
    )
    before(async () => {
        const monitorPath = `/dashboard/monitors/${dummy_all_monitors[0].id}`
        if (router.history.current.path !== monitorPath) await router.push(monitorPath)

        axiosStub = sinon.stub(Vue.prototype.$axios, 'get').returns(
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
            },
        })
    })

    after(async () => {
        await axiosStub.restore()
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

            await axiosStub.should.have.been.calledWith(`/monitors/${id}`)
            await axiosStub.should.have.been.calledWith(
                `/maxscale/modules/${moduleId}?fields[module]=parameters`
            )
            let count = 2
            await serversData.forEach(async server => {
                await axiosStub.should.have.been.calledWith(
                    `/servers/${server.id}?fields[servers]=state`
                )
                ++count
            })

            axiosStub.should.have.callCount(count)
        })
    })
})
