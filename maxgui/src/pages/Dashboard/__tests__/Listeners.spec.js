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
import Listeners from '@/pages/Dashboard/Listeners'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import {
    dummy_all_listeners,
    findAnchorLinkInTable,
    getUniqueResourceNamesStub,
} from '@tests/unit/utils'

chai.should()
chai.use(sinonChai)

const expectedTableHeaders = [
    { text: 'Listener', value: 'id', autoTruncate: true },
    { text: 'Port', value: 'port' },
    { text: 'Host', value: 'address' },
    { text: 'State', value: 'state' },
    { text: 'Service', value: 'serviceIds', autoTruncate: true },
]

const expectedTableRows = [
    {
        id: 'RCR-Router-Listener',
        port: 3308,
        address: '::',
        state: 'Running',
        serviceIds: ['service_0'],
    },
    {
        id: 'RCR-Router-Listener-1',
        port: 3306,
        address: '::',
        state: 'Running',
        serviceIds: ['service_1'],
    },
    {
        id: 'RCR-Router-Listener-2',
        port: null,
        address: '/tmp/maxscale.sock',
        state: 'Running',
        serviceIds: ['service_1'],
    },
]

describe('Dashboard Listeners tab', () => {
    let wrapper, axiosStub

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: Listeners,
            computed: {
                all_listeners: () => dummy_all_listeners,
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$store.$http, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
    })

    afterEach(() => {
        axiosStub.restore()
    })

    it(`Should process table rows accurately`, () => {
        expect(wrapper.vm.tableRows).to.be.deep.equals(expectedTableRows)
    })

    it(`Should pass expected table headers to data-table`, () => {
        const dataTable = wrapper.findComponent({ name: 'data-table' })
        expect(wrapper.vm.tableHeaders).to.be.deep.equals(expectedTableHeaders)
        expect(dataTable.vm.$props.headers).to.be.deep.equals(expectedTableHeaders)
    })

    it(`Should navigate to service detail page when a service is clicked`, async () => {
        const listenerId = dummy_all_listeners[0].id
        const serviceId = dummy_all_listeners[0].relationships.services.data[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: listenerId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'serviceIds'),
        })
        await aTag.trigger('click')
        wrapper.vm.$nextTick(() =>
            expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
        )
    })

    it(`Should navigate to listener detail page when a listener is clicked`, async () => {
        const listenerId = dummy_all_listeners[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: listenerId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'id'),
        })
        await aTag.trigger('click')
        wrapper.vm.$nextTick(() =>
            expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/listeners/${listenerId}`)
        )
    })

    it(`Should get total number of unique service names accurately`, () => {
        const uniqueServiceNames = getUniqueResourceNamesStub(expectedTableRows, 'serviceIds')
        expect(wrapper.vm.$data.servicesLength).to.be.equals(uniqueServiceNames.length)
    })
})
