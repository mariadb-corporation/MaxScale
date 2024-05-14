/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import OverviewHeader from '@src/pages/ServiceDetail/OverviewHeader'
import {
    dummy_all_services,
    dummy_service_connection_datasets,
    dummy_service_connection_info,
} from '@tests/unit/utils'

describe('ServiceDetail - OverviewHeader', () => {
    let wrapper, outlineOverviewCards

    const {
        attributes: { router, started },
    } = dummy_all_services[0]

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: OverviewHeader,
            propsData: {
                currentService: dummy_all_services[0],
                serviceConnectionsDatasets: dummy_service_connection_datasets,
                serviceConnectionInfo: dummy_service_connection_info,
            },
            stubs: { 'line-chart': '<div/>' },
        })
        outlineOverviewCards = wrapper.findAllComponents({
            name: 'outlined-overview-card',
        }).wrappers
    })

    it(`Should render 3 outlined-overview-card components`, () => {
        expect(outlineOverviewCards.length).to.be.equals(3)
    })

    it(`Should show 'Overview' title in the first outlined-overview-card`, () => {
        const title = outlineOverviewCards[0].find('.detail-overview__title')
        expect(title.text()).to.be.equals('Overview')
    })
    it(`Should show 'Overview' title in the first outlined-overview-card`, () => {
        const title = outlineOverviewCards[0].find('.detail-overview__title')
        expect(title.text()).to.be.equals('Overview')
    })
    it(`Should show router in the first outlined-overview-card`, () => {
        const cardTitle = outlineOverviewCards[0].find('.text-caption')
        const cardBody = outlineOverviewCards[0].find('.router')
        expect(cardTitle.text()).to.be.equals('ROUTER')
        expect(cardBody.text()).to.be.equals(router)
    })
    it(`Should show started time in the second outlined-overview-card`, () => {
        const { dateFormat } = wrapper.vm.$helpers
        const cardTitle = outlineOverviewCards[1].find('.text-caption')
        const cardBody = outlineOverviewCards[1].find('.started')
        expect(cardTitle.text()).to.be.equals('STARTED AT')
        expect(cardBody.text()).to.be.equals(dateFormat({ value: started }))
    })
    it(`Should not show tile in the last outlined-overview-card`, () => {
        expect(outlineOverviewCards[2].vm.$props.tile).to.be.false
    })
    it(`Should current connections as title in the last outlined-overview-card`, () => {
        const title = outlineOverviewCards[2].find('.detail-overview__title')
        const { connections, total_connections } = dummy_service_connection_info
        expect(title.text()).to.be.equals(
            `Current Connections  (${connections}/${total_connections})`
        )
    })
    it(`Should show connections chart in the last outlined-overview-card`, () => {
        const lineChart = outlineOverviewCards[2].findComponent({
            name: 'stream-line-chart',
        })
        expect(lineChart.exists()).to.be.true
    })
})
