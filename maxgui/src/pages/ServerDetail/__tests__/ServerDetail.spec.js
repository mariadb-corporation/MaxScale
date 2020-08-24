/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import chai /* , { expect }  */ from 'chai'
import mount, { router } from '@tests/unit/setup'
import ServerDetail from '@/pages/ServerDetail'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { dummy_all_servers, testRelationshipUpdate } from '@tests/unit/utils'
chai.should()
chai.use(sinonChai)

describe('ServerDetail index', () => {
    let wrapper, axiosGetStub, axiosPatchStub

    before(async () => {
        const serverPath = `/dashboard/servers/${dummy_all_servers[0].id}`
        if (router.history.current.path !== serverPath) await router.push(serverPath)

        axiosGetStub = sinon.stub(Vue.prototype.$axios, 'get').returns(
            Promise.resolve({
                data: {},
            })
        )

        wrapper = mount({
            shallow: true,
            component: ServerDetail,
            computed: {
                current_server: () => dummy_all_servers[0],
            },
        })
        axiosPatchStub = sinon.stub(wrapper.vm.$axios, 'patch').returns(Promise.resolve())
    })

    after(async () => {
        await axiosGetStub.restore()
        await axiosPatchStub.restore()
    })

    it(`Should send request to get current server, relationships services state`, async () => {
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

            axiosGetStub.should.have.callCount(count)
        })
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
})
