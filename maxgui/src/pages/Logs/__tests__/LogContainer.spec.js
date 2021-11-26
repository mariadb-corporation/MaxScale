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
import '@/plugins/vuex'
import store from 'store'
import chai, { expect } from 'chai'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import mount from '@tests/unit/setup'
import LogContainer from '@/pages/Logs/LogContainer'
import { dummy_log_data } from '@tests/unit/utils'

chai.should()
chai.use(sinonChai)

const dummyChosenLogLevels = ['warning']

const mountFactory = opts =>
    mount({
        shallow: false,
        component: LogContainer,
        props: {
            logViewHeight: 500,
            chosenLogLevels: [],
        },
        computed: {
            prev_log_link: () => null, // prevent loopGetOlderLogs from being called
            logToShow: () => dummy_log_data,
        },
        ...opts,
    })

// mockup websocket
class WebSocket {
    constructor() {}
    onmessage() {}
    close() {}
    onopen() {}
}

global.WebSocket = WebSocket

describe('LogContainer', () => {
    let wrapper, axiosStub, wsStub
    beforeEach(async () => {
        wsStub = sinon.stub(window, 'WebSocket')
        axiosStub = sinon.stub(store.$http, 'get').returns(
            Promise.resolve({
                data: {
                    data: {
                        attributes: { log: dummy_log_data, log_source: 'syslog' },
                    },
                },
            })
        )
        wrapper = mountFactory()
        /* async setTimeout doesn't work properly in vue-test-utils as
         * there is no actual async lifecycle hooks in vue.js. So
         * allLogData will be always an empty array.
         * This is a workaround to delay 350ms after mounted hook is called,
         * so allLogData will be assigned with dummy_log_data
         */
        await wrapper.vm.$help.delay(350)
    })
    afterEach(async function() {
        await axiosStub.restore()
        await wsStub.restore()
        await wrapper.destroy()
    })

    it(`Should send requests to get maxscale log`, async () => {
        await axiosStub.should.have.been.calledWith('/maxscale/logs/data?page[size]=100')
        axiosStub.should.have.been.called
    })

    it(`Should return accurate boolean value for computed property 'isFiltering'`, async () => {
        expect(wrapper.vm.isFiltering).to.be.false
        wrapper.vm.$store.commit('maxscale/SET_CHOSEN_LOG_LEVELS', dummyChosenLogLevels)
        expect(wrapper.vm.isFiltering).to.be.true
    })

    it(`Should show no logs found when logToShow is empty`, async () => {
        wrapper = mountFactory({
            computed: {
                logToShow: () => [],
            },
        })
        expect(wrapper.html().includes('No logs found'))
    })

    it(`Should return accurate log data for computed property 'logToShow'`, async () => {
        expect(wrapper.vm.logToShow).to.be.deep.equals(dummy_log_data)

        await wrapper.setProps({
            chosenLogLevels: dummyChosenLogLevels,
        })
    })
})
