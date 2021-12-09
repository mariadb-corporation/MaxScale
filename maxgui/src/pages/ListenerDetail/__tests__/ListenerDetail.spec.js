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
import chai from 'chai'
import mount, { router } from '@tests/unit/setup'
import ListenerDetail from '@/pages/ListenerDetail'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { dummy_all_listeners } from '@tests/unit/utils'
chai.should()
chai.use(sinonChai)

describe('ListenerDetail index', () => {
    let wrapper, axiosStub

    before(async () => {
        const listenerPath = `/dashboard/listeners/${dummy_all_listeners[0].id}`
        if (router.history.current.path !== listenerPath) await router.push(listenerPath)
    })

    beforeEach(async () => {
        axiosStub = sinon.stub(store.$http, 'get').returns(
            Promise.resolve({
                data: {},
            })
        )
        wrapper = mount({
            shallow: false,
            component: ListenerDetail,
            computed: {
                current_listener: () => dummy_all_listeners[0],
            },
        })
    })
    afterEach(() => {
        axiosStub.restore()
    })

    it(`Should send request to get listener, relationships service state
      and module parameters`, async () => {
        await wrapper.vm.$nextTick(async () => {
            let {
                id,
                attributes: {
                    parameters: { protocol },
                },
                relationships: {
                    services: { data: servicesData },
                },
            } = dummy_all_listeners[0]

            await axiosStub.should.have.been.calledWith(`/listeners/${id}`)

            await servicesData.forEach(async service => {
                await axiosStub.should.have.been.calledWith(
                    `/services/${service.id}?fields[services]=state`
                )
            })

            await axiosStub.should.have.been.calledWith(
                `/maxscale/modules/${protocol}?fields[module]=parameters`
            )

            axiosStub.should.have.callCount(3)
        })
    })
})
