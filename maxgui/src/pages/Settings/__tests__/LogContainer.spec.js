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
import chai, { expect /* , { expect }  */ } from 'chai'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import mount from '@tests/unit/setup'
import LogContainer from '@/pages/Settings/LogContainer'
chai.should()
chai.use(sinonChai)

const dummyMaxscaleOverviewInfo = {
    activated_at: 'Wed, 23 Sep 2020 13:35:59 GMT',
    commit: '5061f7bd14ea3f6dd814b704f2309f806a878a60',
    started_at: 'Wed, 23 Sep 2020 13:35:59 GMT',
    uptime: 404,
    version: '2.5.4',
}
const mountFactory = () =>
    mount({
        shallow: false,
        component: LogContainer,
        props: {
            shouldFetchLogs: false,
            maxscaleOverviewInfo: dummyMaxscaleOverviewInfo,
        },
    })

describe('LogContainer', () => {
    let wrapper, axiosStub
    beforeEach(async () => {
        axiosStub = sinon.stub(Vue.prototype.$axios, 'get').returns(
            Promise.resolve({
                data: {},
            })
        )
        wrapper = mountFactory()
    })
    afterEach(async function() {
        await axiosStub.restore()
        await wrapper.destroy()
    })

    it(`Should send request to get maxscale log on mounted`, async () => {
        await axiosStub.should.have.been.calledOnceWith('/maxscale/logs')
        axiosStub.should.have.been.calledOnce
    })
    it(`Should send request to get maxscale log when shouldFetchLogs is true`, async () => {
        await wrapper.setData({
            shouldFetchLogs: true,
        })
        await axiosStub.should.have.been.calledWith('/maxscale/logs')
        axiosStub.should.have.been.calledTwice
    })

    it(`Should pass logData to log-view component`, async () => {
        const logView = wrapper.findComponent({ name: 'log-view' })
        expect(logView.vm.$props.logData).to.be.deep.equals(wrapper.vm.$data.logData)
    })
})
