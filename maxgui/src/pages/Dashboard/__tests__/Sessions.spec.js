/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import Sessions from '@/pages/Dashboard/Sessions'

import { dummy_all_sessions, getUniqueResourceNamesStub } from '@tests/unit/utils'

const expectedTableHeaders = [
    { text: 'ID', value: 'id' },
    { text: 'Client', value: 'user' },
    { text: 'Connected', value: 'connected' },
    { text: 'IDLE (s)', value: 'idle' },
    { text: 'Service', value: 'serviceIds' },
]

const expectedTableRows = [
    {
        id: '1000002',
        user: 'maxskysql@::ffff:127.0.0.1',
        connected: '14:06:17 08.13.2020',
        idle: 55.5,
        serviceIds: ['RCR-Router'],
    },
]

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

    it(`Should pass expected table headers to sessions-table`, () => {
        const dataTable = wrapper.findComponent({ name: 'sessions-table' })
        expect(wrapper.vm.tableHeaders).to.be.deep.equals(expectedTableHeaders)
        expect(dataTable.vm.$attrs.headers).to.be.deep.equals(expectedTableHeaders)
    })

    it(`Should get total number of unique service names accurately`, () => {
        const uniqueServiceNames = getUniqueResourceNamesStub(expectedTableRows, 'serviceIds')
        expect(wrapper.vm.$data.servicesLength).to.be.equals(uniqueServiceNames.length)
    })
})
