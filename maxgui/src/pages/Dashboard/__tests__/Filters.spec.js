/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import Filters from '@/pages/Dashboard/Filters'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import {
    dummy_all_filters,
    findAnchorLinkInTable,
    getUniqueResourceNamesStub,
} from '@tests/unit/utils'

chai.should()
chai.use(sinonChai)

const expectedTableHeaders = [
    { text: 'Filter', value: 'id', autoTruncate: true },
    { text: 'Service', value: 'serviceIds', autoTruncate: true },
    { text: 'Module', value: 'module' },
]

const expectedTableRows = [
    {
        id: 'filter_0',
        serviceIds: ['service_0', 'service_1'],
        module: 'qlafilter',
    },
    {
        id: 'filter_1',
        serviceIds: ['service_1'],
        module: 'binlogfilter',
    },
]

describe('Dashboard Filters tab', () => {
    let wrapper, axiosStub

    after(async () => {
        await axiosStub.reset()
    })

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: Filters,
            computed: {
                all_filters: () => dummy_all_filters,
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$store.$http, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
    })

    afterEach(async function() {
        await axiosStub.restore()
    })

    it(`Should process table rows accurately`, async () => {
        expect(wrapper.vm.tableRows).to.be.deep.equals(expectedTableRows)
    })

    it(`Should pass expected table headers to data-table`, async () => {
        const dataTable = wrapper.findComponent({ name: 'data-table' })
        expect(wrapper.vm.tableHeaders).to.be.deep.equals(expectedTableHeaders)
        expect(dataTable.vm.$props.headers).to.be.deep.equals(expectedTableHeaders)
    })

    it(`Should navigate to service detail page when a service is clicked`, async () => {
        const filterId = dummy_all_filters[1].id
        const serviceId = dummy_all_filters[1].relationships.services.data[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: filterId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'serviceIds'),
        })
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
    })

    it(`Should navigate to filter detail page when a filter is clicked`, async () => {
        const filterId = dummy_all_filters[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: filterId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'id'),
        })
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/filters/${filterId}`)
    })

    it(`Should get total number of unique service names accurately`, async () => {
        const uniqueServiceNames = getUniqueResourceNamesStub(expectedTableRows, 'serviceIds')
        expect(wrapper.vm.$data.servicesLength).to.be.equals(uniqueServiceNames.length)
    })
})
