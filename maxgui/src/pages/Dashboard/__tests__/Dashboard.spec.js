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
import Dashboard from '@src/pages/Dashboard'

describe('Dashboard', () => {
    it(`Should call fetchMaxScaleOverviewInfo and fetchAll`, () => {
        let count = 0,
            fetchAllCount = 0
        let wrapper = mount({
            shallow: true,
            component: Dashboard,
            methods: { fetchMaxScaleOverviewInfo: () => count++, fetchAll: () => fetchAllCount++ },
        })
        expect(count).to.be.equals(1)
        wrapper.vm.$nextTick(() => expect(fetchAllCount).to.be.equals(1))
        wrapper.destroy()
    })
    describe('Dashboard child components rendering tests', () => {
        let wrapper
        before(() => {
            wrapper = mount({
                shallow: true,
                component: Dashboard,
                methods: { fetchMaxScaleOverviewInfo: () => null, fetchAll: () => null },
            })
        })
        const components = ['page-wrapper', 'page-header', 'graphs']
        components.forEach(name =>
            it(`Should render ${name} component`, () => {
                expect(wrapper.findComponent({ name }).exists()).to.be.true
            })
        )
    })

    describe('Dashboard - method tests', () => {
        it(`Should call fetchAll and updateChart when onCountDone is called`, async () => {
            const wrapper = mount({
                shallow: false,
                component: Dashboard,
                methods: { fetchMaxScaleOverviewInfo: () => null, fetchAll: () => null },
                stubs: {
                    'refresh-rate': "<div class='refresh-rate'></div>",
                    'page-header': "<div class='page-header'></div>",
                    'line-chart': '<div/>',
                },
            })
            const fetchAllSpy = sinon.spy(wrapper.vm, 'fetchAll')
            const updateChartSpy = sinon.spy(wrapper.vm.$refs.graphs, 'updateChart')
            await wrapper.vm.onCountDone()
            fetchAllSpy.should.have.been.called
            updateChartSpy.should.have.been.called
        })
    })
})
