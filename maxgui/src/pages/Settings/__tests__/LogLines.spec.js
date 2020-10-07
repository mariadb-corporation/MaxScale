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
import { dummy_log_data } from '@tests/unit/utils'

const dummyChosenLogLevels = ['debug']
const dummyFilteredLog = dummy_log_data.filter(log => dummyChosenLogLevels.includes(log.priority))

const mountFactory = () =>
    mount({
        shallow: false,
        component: LogLines,
        props: {
            allLogData: dummy_log_data,
            isLoading: false,
            filteredLog: [],
            chosenLogLevels: [],
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
    it(`Should show no logs found when there logToShow is empty`, async () => {
        await wrapper.setProps({
            isLoading: false, // loading state is false indicates loading is done
            allLogData: [],
        })
        expect(wrapper.html().includes('No logs found'))
    })

    it(`Should return accurate boolean value for computed property 'isFiltering'`, async () => {
        expect(wrapper.vm.isFiltering).to.be.false
        await wrapper.setProps({
            chosenLogLevels: dummyChosenLogLevels,
            filteredLog: dummyFilteredLog,
        })
        expect(wrapper.vm.isFiltering).to.be.true
    })

    it(`Should return accurate log data for computed property 'logToShow'`, async () => {
        expect(wrapper.vm.logToShow).to.be.deep.equals(dummy_log_data)
        await wrapper.setProps({
            chosenLogLevels: dummyChosenLogLevels,
            filteredLog: dummyFilteredLog,
        })
        expect(wrapper.vm.logToShow).to.be.deep.equals(dummyFilteredLog)
    })
})
