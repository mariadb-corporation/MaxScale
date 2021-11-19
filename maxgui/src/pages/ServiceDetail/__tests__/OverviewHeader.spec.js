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

import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import OverviewHeader from '@/pages/ServiceDetail/OverviewHeader'
import {
    dummy_all_services,
    dummy_service_connection_datasets,
    dummy_service_connection_info,
} from '@tests/unit/utils'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
chai.should()
chai.use(sinonChai)

const defaultProps = {
    currentService: dummy_all_services[0],
    serviceConnectionsDatasets: dummy_service_connection_datasets,
    serviceConnectionInfo: dummy_service_connection_info,
}

const propsMountFactory = props =>
    mount({
        shallow: false,
        component: OverviewHeader,
        props: props,
    })

describe('ServiceDetail - OverviewHeader', () => {
    let wrapper, clock, updateChartSpy
    beforeEach(async () => {
        clock = sinon.useFakeTimers()
        updateChartSpy = sinon.spy(OverviewHeader.methods, 'updateChart')
        wrapper = mount({
            shallow: false,
            component: OverviewHeader,
            props: defaultProps,
        })
    })
    afterEach(async function() {
        await clock.restore()
        await updateChartSpy.restore()
        // this prevent fetch loop in line-chart-stream
        await wrapper.setData({
            options: null,
        })
        await wrapper.destroy()
    })
    it(`Should emit update-chart event`, async () => {
        let count = 0
        wrapper.vm.$on('update-chart', () => {
            count++
        })
        //mockup update chart
        await wrapper.vm.updateChart()
        expect(count).to.be.equals(1)
    })

    it(`Should call updateChart after 10s`, async () => {
        await clock.tick(1000)
        expect(updateChartSpy).to.not.have.been.called
        await clock.tick(9000)
        expect(updateChartSpy).to.have.been.calledOnce
    })

    describe('outlined-overview-card render assertions', () => {
        const {
            attributes: { router, started },
        } = dummy_all_services[0]
        let outlineOverviewCards
        beforeEach(async () => {
            wrapper = propsMountFactory(defaultProps)
            outlineOverviewCards = wrapper.findAllComponents({
                name: 'outlined-overview-card',
            }).wrappers
        })
        afterEach(async function() {
            // this prevent fetch loop in lline-chart-stream
            await wrapper.setData({
                options: null,
            })
            await wrapper.destroy()
        })

        it(`Should render 3 outlined-overview-card components`, async () => {
            expect(outlineOverviewCards.length).to.be.equals(3)
        })

        it(`Should show 'Overview' title in the first outlined-overview-card`, async () => {
            const title = outlineOverviewCards[0].find('.detail-overview__title')
            expect(title.text()).to.be.equals('Overview')
        })
        it(`Should show 'Overview' title in the first outlined-overview-card`, async () => {
            const title = outlineOverviewCards[0].find('.detail-overview__title')
            expect(title.text()).to.be.equals('Overview')
        })
        it(`Should show router in the first outlined-overview-card`, async () => {
            const cardTitle = outlineOverviewCards[0].find('.text-caption')
            const cardBody = outlineOverviewCards[0].find('.router')
            expect(cardTitle.text()).to.be.equals('ROUTER')
            expect(cardBody.text()).to.be.equals(router)
        })
        it(`Should show started time in the second outlined-overview-card`, async () => {
            const { dateFormat } = wrapper.vm.$help
            const cardTitle = outlineOverviewCards[1].find('.text-caption')
            const cardBody = outlineOverviewCards[1].find('.started')
            expect(cardTitle.text()).to.be.equals('STARTED AT')
            expect(cardBody.text()).to.be.equals(dateFormat({ value: started }))
        })
        it(`Should not show tile in the last outlined-overview-card`, async () => {
            expect(outlineOverviewCards[2].vm.$props.tile).to.be.false
        })
        it(`Should current connections as title in the last outlined-overview-card`, async () => {
            const title = outlineOverviewCards[2].find('.detail-overview__title')
            const { connections, total_connections } = dummy_service_connection_info
            expect(title.text()).to.be.equals(
                `Current Connections  (${connections}/${total_connections})`
            )
        })
        it(`Should show connections chart in the last outlined-overview-card`, async () => {
            const lineChart = outlineOverviewCards[2].findComponent({ name: 'line-chart-stream' })
            expect(lineChart.exists()).to.be.true
        })
    })
})
