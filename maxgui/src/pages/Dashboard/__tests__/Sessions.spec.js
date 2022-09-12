/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import Sessions from '@rootSrc/pages/Dashboard/Sessions'

import {
    dummy_all_sessions,
    findAnchorLinkInTable,
    getUniqueResourceNamesStub,
} from '@tests/unit/utils'

const expectedTableHeaders = [
    { text: 'ID', value: 'id' },
    { text: 'Client', value: 'user' },
    { text: 'Connected', value: 'connected' },
    { text: 'IDLE (s)', value: 'idle' },
    { text: 'Memory', value: 'memory' },
    { text: 'Service', value: 'serviceIds' },
]

describe('Dashboard Sessions tab', () => {
    let wrapper, axiosStub

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: Sessions,
            computed: {
                all_sessions: () => dummy_all_sessions,
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$http, 'get').resolves(Promise.resolve({ data: {} }))
    })

    afterEach(() => {
        axiosStub.restore()
    })

    it(`Should process table rows accurately`, () => {
        expect(wrapper.vm.tableRows[0]).to.include.all.keys(
            'id',
            'user',
            'connected',
            'idle',
            'serviceIds',
            'memory'
        )
        expect(wrapper.vm.tableRows[0].memory).to.be.an('object')
        expect(wrapper.vm.tableRows[0].serviceIds).to.be.an('array')
    })

    it(`Should pass expected table headers to sessions-table`, () => {
        const sessionsTable = wrapper.findComponent({ name: 'sessions-table' })
        expect(sessionsTable.vm.$props.headers).to.be.deep.equals(expectedTableHeaders)
    })

    it(`Should navigate to service detail page when a service is clicked`, async () => {
        const sessionId = dummy_all_sessions[0].id
        const serviceId = dummy_all_sessions[0].relationships.services.data[0].id
        const dataTable = wrapper.findComponent({ name: 'sessions-table' })
        const aTag = findAnchorLinkInTable({
            wrapper: dataTable,
            rowId: sessionId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'serviceIds'),
        })
        await aTag.trigger('click')
        await wrapper.vm.$nextTick()
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
    })

    it(`Should get total number of unique service names accurately`, () => {
        const uniqueServiceNames = getUniqueResourceNamesStub(wrapper.vm.tableRows, 'serviceIds')
        expect(wrapper.vm.$data.servicesLength).to.be.equals(uniqueServiceNames.length)
    })
})
