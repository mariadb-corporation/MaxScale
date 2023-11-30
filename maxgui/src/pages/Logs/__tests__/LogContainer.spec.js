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
import mount from '@tests/unit/setup'
import store from '@rootSrc/store'
import LogContainer from '@rootSrc/pages/Logs/LogContainer'
import { dummy_log_data } from '@tests/unit/utils'

const dummyChosenLogLevels = ['warning']

const mountFactory = opts =>
    mount({
        shallow: false,
        component: LogContainer,
        propsData: { logViewHeight: 500 },
        computed: {
            prev_log_link: () => null, // prevent loopGetOlderLogs from being called
            latest_logs: () => dummy_log_data,
        },
        stubs: {
            'virtual-list': '<div/>',
        },
        methods: {
            checkOverFlow: sinon.stub(),
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
        axiosStub = sinon.stub(store.vue.$http, 'get').returns(
            Promise.resolve({
                data: { data: { attributes: { log: dummy_log_data, log_source: 'syslog' } } },
            })
        )
        wrapper = mountFactory()
        /* async setTimeout doesn't work properly in vue-test-utils as
         * there is no actual async lifecycle hooks in vue.js. So
         * allLogData will be always an empty array.
         * This is a workaround to delay 350ms after mounted hook is called,
         * so allLogData will be assigned with dummy_log_data
         */
        await wrapper.vm.$helpers.delay(350)
    })
    afterEach(() => {
        axiosStub.restore()
        wsStub.restore()
        wrapper.destroy()
    })

    it(`Should send requests to get maxscale log`, async () => {
        await axiosStub.should.have.been.calledWith('/maxscale/logs/data?page[size]=100')
        axiosStub.should.have.been.called
    })

    it(`Should return accurate boolean value for computed property 'isFiltering'`, async () => {
        wrapper = mountFactory({ computed: { getChosenLogLevels: () => dummyChosenLogLevels } })
        expect(wrapper.vm.isFiltering).to.be.true
    })

    it(`Should show no logs found when logToShow is empty`, () => {
        wrapper = mountFactory({ computed: { logToShow: () => [] } })
        expect(wrapper.html().includes('No logs found'))
    })

    const logToShowTestCases = [
        { isFiltering: true, expected: 'filteredLogData' },
        { isFiltering: false, expected: 'allLogData' },
    ]
    logToShowTestCases.forEach(({ isFiltering, expected }) => {
        it(`logToShow should return ${expected}`, () => {
            wrapper = mountFactory({ computed: { isFiltering: () => isFiltering } })
            expect(wrapper.vm.logToShow).to.eql(wrapper.vm[expected])
        })
    })
})
