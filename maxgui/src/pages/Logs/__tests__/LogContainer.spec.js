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
import LogContainer from '@src/pages/Logs/LogContainer'
import { dummy_log_data } from '@tests/unit/utils'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: LogContainer,
                propsData: { logViewHeight: 500 },
                computed: {
                    prev_log_link: () => '', // prevent loopGetOlderLogs from being called
                    latest_logs: () => dummy_log_data,
                },
                stubs: {
                    'virtual-list': '<div/>',
                },
                methods: {
                    detectScrollability: sinon.stub(),
                    fetchLatestLogs: sinon.stub(),
                    fetchPrevLogs: sinon.stub(),
                    openConnection: sinon.stub(),
                },
            },
            opts
        )
    )

describe('LogContainer', () => {
    let wrapper
    afterEach(() => wrapper.destroy())

    it(`Should call handleFetchLogs on created hook`, () => {
        const spy = sinon.spy(LogContainer.methods, 'handleFetchLogs')
        wrapper = mountFactory()
        spy.should.have.been.calledOnce
    })

    it(`Should show no logs found when logs is empty`, () => {
        wrapper = mountFactory({ data: () => ({ logs: [] }) })
        expect(wrapper.html().includes('No logs found'))
    })
})
