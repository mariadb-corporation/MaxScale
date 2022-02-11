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

import mount from '@tests/unit/setup'
import TableHeader from '@/components/common/DataTable/TableHeader'

describe('TableHeader.vue', () => {
    let wrapper
    let headers = [
        { text: `Monitor`, value: 'groupId' },
        { text: 'State', value: 'monitorState' },
        { text: 'Servers', value: 'id' },
        { text: 'Address', value: 'serverAddress' },
        { text: 'Port', value: 'serverPort' },
        { text: 'Connections', value: 'serverConnections' },
        { text: 'State', value: 'serverState' },
        { text: 'GTID', value: 'gtid' },
        { text: 'Services', value: 'serviceIds' },
    ]

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: TableHeader,
            propsData: {
                headers: headers,
                sortBy: 'groupId',
                sortDesc: true,
                isTree: false,
                hasValidChild: false,
            },
        })
    })

    it('All columns have sortable class', () => {
        expect(wrapper.findAll('.sortable').length).to.equal(9)
    })

    it('Last column has not-sortable class and other columns have sortable class', async () => {
        let newHeaders = wrapper.vm.$help.lodash.cloneDeep(headers)
        newHeaders[newHeaders.length - 1].sortable = false
        await wrapper.setProps({
            headers: newHeaders,
        })
        expect(wrapper.findAll('.sortable').length).to.equal(8)
        expect(wrapper.findAll('.not-sortable').length).to.equal(1)
    })
})
