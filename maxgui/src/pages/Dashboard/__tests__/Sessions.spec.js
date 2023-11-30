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
import Sessions from '@rootSrc/pages/Dashboard/Sessions'

import { dummy_all_sessions, getUniqueResourceNamesStub } from '@tests/unit/utils'

describe('Dashboard Sessions tab', () => {
    let wrapper, axiosStub

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: Sessions,
            computed: {
                current_sessions: () => dummy_all_sessions,
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
        expect(sessionsTable.vm.$props.extraHeaders).to.be.deep.equals([
            { text: 'Service', value: 'serviceIds' },
        ])
    })

    it(`Should get total number of unique service names accurately`, () => {
        const uniqueServiceNames = getUniqueResourceNamesStub(wrapper.vm.tableRows, 'serviceIds')
        expect(wrapper.vm.$data.servicesLength).to.be.equals(uniqueServiceNames.length)
    })
})
