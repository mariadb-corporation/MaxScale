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
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import mount from '@tests/unit/setup'
import Logs from '@/pages/Logs'

chai.should()
chai.use(sinonChai)

describe('Logs index', () => {
    let wrapper, axiosStub
    beforeEach(async () => {
        axiosStub = sinon.stub(store.$http, 'get').returns(
            Promise.resolve({
                data: {
                    data: {},
                },
            })
        )
        wrapper = mount({
            shallow: false,
            component: Logs,
        })
    })
    afterEach(async function() {
        await axiosStub.restore()
        await wrapper.destroy()
    })

    it(`Should send requests to get maxscale overview info`, async () => {
        await axiosStub.should.have.been.calledWith(
            '/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime'
        )
        axiosStub.should.have.been.called
    })
})
