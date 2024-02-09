/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ResTimeDistHistogram from '@src/pages/ServerDetail/ResTimeDistHistogram'
import { lodash } from '@share/utils/helpers'

const resTimeDistStub = {
    read: {
        distribution: [
            { count: 2970, time: '0.000001', total: 0 },
            { count: 0, time: '0.000010', total: 0 },
            { count: 116981, time: '0.000100', total: 10.283463914 },
            { count: 1780646, time: '0.001000', total: 449.011574664 },
            { count: 6741, time: '0.010000', total: 8.722483163 },
            { count: 0, time: '0.100000', total: 0 },
            { count: 0, time: '1.000000', total: 0 },
            { count: 3, time: '10.000000', total: 6.465117095 },
            { count: 0, time: '100.000000', total: 0 },
            { count: 0, time: '1000.000000', total: 0 },
            { count: 0, time: '10000.000000', total: 0 },
            { count: 0, time: '100000.000000', total: 0 },
        ],
        operation: 'read',
        range_base: 10,
    },
    write: {
        distribution: [
            { count: 772, time: '0.000001', total: 0 },
            { count: 0, time: '0.000010', total: 0 },
            { count: 21205, time: '0.000100', total: 1.874781114 },
            { count: 1960824, time: '0.001000', total: 562.014347937 },
            { count: 16773, time: '0.010000', total: 24.410401518 },
            { count: 455, time: '0.100000', total: 11.456366698 },
            { count: 0, time: '1.000000', total: 0 },
            { count: 0, time: '10.000000', total: 0 },
            { count: 0, time: '100.000000', total: 0 },
            { count: 0, time: '1000.000000', total: 0 },
            { count: 0, time: '10000.000000', total: 0 },
            { count: 0, time: '100000.000000', total: 0 },
        ],
        operation: 'write',
        range_base: 10,
    },
}
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: ResTimeDistHistogram,
                propsData: { resTimeDist: resTimeDistStub },
            },
            opts
        )
    )

describe('ResTimeDistHistogram', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        beforeEach(() => (wrapper = mountFactory()))
        it('Should pass accurate data to collapsible-ctr component', () => {
            expect(
                wrapper.findComponent({
                    name: 'collapsible-ctr',
                }).vm.$props.title
            ).to.equal(wrapper.vm.$mxs_t('resTimeDist'))
        })

        it('Should pass accurate data to mxs-bar-chart component', () => {
            const {
                $attrs: { chartData },
                $props: { opts },
            } = wrapper.findComponent({
                name: 'mxs-bar-chart',
            }).vm
            expect(chartData).to.eql(wrapper.vm.chartData)
            expect(opts).to.eql(wrapper.vm.chartOptions)
        })
    })
    describe(`Computed properties and method tests`, () => {
        beforeEach(() => (wrapper = mountFactory()))
        it('labels should be an array', () => {
            const labels = wrapper.vm.labels
            expect(labels).to.be.an('array')
            expect(labels).to.eql(resTimeDistStub.read.distribution.map(item => item.time))
        })

        it('Should generate chart data correctly', () => {
            const chartData = wrapper.vm.chartData
            expect(chartData.datasets).to.have.lengthOf(2)
            expect(chartData.datasets[0].label).to.equal('Read')
            expect(chartData.datasets[1].label).to.equal('Write')
            expect(chartData.datasets[0].data).to.eql(resTimeDistStub.read.distribution)
            expect(chartData.datasets[1].data).to.eql(resTimeDistStub.write.distribution)
        })

        it('parsing should return expected key value', () => {
            expect(wrapper.vm.parsing).to.eql({ xAxisKey: 'time', yAxisKey: 'count' })
        })
    })
})
