/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer'
import { lodash } from '@share/utils/helpers'

const executionTimeStub = 0.00004
const startTimeStub = new Date().valueOf()
const totalDurationStub = 4.00004
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: DurationTimer,
                propsData: {
                    executionTime: executionTimeStub,
                    startTime: startTimeStub,
                    totalDuration: totalDurationStub,
                },
            },
            opts
        )
    )

describe('duration-timer', () => {
    let wrapper

    const renderTestCases = [
        { attr: 'exe-time', label: 'exeTime', valueAttr: 'executionTime' },
        { attr: 'latency-time', label: 'latency', valueAttr: 'latency' },
        { attr: 'total-time', label: 'total', valueAttr: 'duration' },
    ]
    renderTestCases.forEach(({ attr, valueAttr }) => {
        it(`Should render ${attr}`, () => {
            wrapper = mountFactory()
            const expectedStr = `${wrapper.vm[valueAttr]} sec`
            expect(wrapper.find(`[data-test="${attr}"]`).html()).to.contain(expectedStr)
        })
    })

    renderTestCases.forEach(({ attr, label }) => {
        if (label !== 'total')
            it(`${attr} value should be N/A when isGettingEndTime is true`, () => {
                wrapper = mountFactory({ computed: { isGettingEndTime: () => true } })
                expect(wrapper.find(`[data-test="${attr}"]`).html()).to.contain('N/A')
            })
    })

    it('Should return true for isGettingEndTime when totalDuration is 0', () => {
        wrapper = mountFactory({ computed: { totalDuration: () => 0 } })
        expect(wrapper.vm.isGettingEndTime).to.be.true
    })

    it('Should return false for isGettingEndTime when totalDuration is known', () => {
        wrapper = mountFactory({ computed: { totalDuration: () => 3 } })
        expect(wrapper.vm.isGettingEndTime).to.be.false
    })

    it('Should calculate the latency correctly', () => {
        wrapper = mountFactory({
            propsData: { executionTime: executionTimeStub },
            data: () => ({ duration: totalDurationStub }),
        })
        expect(wrapper.vm.latency).to.equal(
            Math.abs(totalDurationStub - executionTimeStub).toFixed(4)
        )
    })
})
