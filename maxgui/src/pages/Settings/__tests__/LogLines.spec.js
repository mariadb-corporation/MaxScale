/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import LogLines from '@/pages/Settings/LogLines'

const dummy_log_data = [
    {
        id: 0,
        message: 'An alert log',
        priority: 'alert',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 1,
        message: 'An error log',
        priority: 'error',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 2,
        message: 'A warning log',
        priority: 'warning',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 3,
        message: 'A notice log',
        priority: 'notice',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 4,
        message: 'An info log',
        priority: 'info',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 5,
        message: 'A debug log',
        priority: 'debug',
        timestamp: '2020-09-23 11:04:27',
    },
]

const mountFactory = () =>
    mount({
        shallow: false,
        component: LogLines,
        props: {
            allLogData: dummy_log_data,
            isLoading: false,
        },
    })

describe('LogLines', () => {
    let wrapper
    beforeEach(async () => {
        wrapper = mountFactory()
    })
    afterEach(async function() {
        await wrapper.destroy()
    })
    it(`Should return accurate color classes for log level section`, async () => {
        const { wrappers: logLevelEls } = wrapper.findAll('.log-level')
        logLevelEls.forEach((ele, i) => {
            const classes = ele.classes().join(' ')
            expect(classes).to.be.equals(
                `log-level d-inline-flex justify-start color text-${dummy_log_data[i].priority} ${
                    dummy_log_data[i].priority === 'alert' ? 'font-weight-bold' : ''
                }`
            )
        })
    })
    it(`Should assign accurate classes for log message section`, async () => {
        const { wrappers: logMsgs } = wrapper.findAll('.text-wrap')
        logMsgs.forEach((logMsg, i) => {
            const classes = logMsg.classes().join(' ')
            expect(classes).to.be.equals(
                `text-wrap color text-${dummy_log_data[i].priority} ${
                    dummy_log_data[i].priority === 'alert' ? 'font-weight-bold' : ''
                }`
            )
        })
    })
    it(`Should show no logs found when there is no allLogData`, async () => {
        await wrapper.setProps({
            isLoading: false, // loading state is false indicates loading is done
            allLogData: [],
        })
        expect(wrapper.html().includes('No logs found'))
    })
})
