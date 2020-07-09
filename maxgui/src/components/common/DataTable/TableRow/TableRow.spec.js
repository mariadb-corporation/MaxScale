/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import TableRow from '@/components/common/DataTable/TableRow'

describe('TableRow.vue', () => {
    let wrapper

    // mockup parent value passing to Collapse

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: TableRow,
            props: {
                rowIndex: 0,
                editableCell: false,
                draggable: false,
                showActionsOnHover: false,
                lastPageItemIndex: 1,
            },
        })
    })

    it(`Component returns accurate value of indexOfHoveredRow to cell slot when 
      draggable or showActionsOnHover is true`, async () => {
        const tr = wrapper.findAll('tr')
        expect(tr.length).to.equal(1)

        await wrapper.setProps({
            showActionsOnHover: true,
            rowIndex: 2,
            lastPageItemIndex: 2,
        })

        // check when showActionsOnHover is true
        tr.trigger('mouseenter')
        expect(wrapper.vm.$data.indexOfHoveredRow).to.equal(2)
        tr.trigger('mouseleave')
        expect(wrapper.vm.$data.indexOfHoveredRow).to.equal(null)

        // when showActionsOnHover = false; draggable = true
        await wrapper.setProps({
            showActionsOnHover: false,
            draggable: true,
            rowIndex: 1,
            lastPageItemIndex: 1,
        })

        await tr.trigger('mouseenter')
        expect(wrapper.vm.$data.indexOfHoveredRow).to.equal(1)
        await tr.trigger('mouseleave')
        expect(wrapper.vm.$data.indexOfHoveredRow).to.equal(null)

        // check when both showActionsOnHover and draggable  are true
        await wrapper.setProps({
            showActionsOnHover: true,
            draggable: true,
            rowIndex: 10,
            lastPageItemIndex: 10,
        })
        await tr.trigger('mouseenter')
        expect(wrapper.vm.$data.indexOfHoveredRow).to.equal(10)
        await tr.trigger('mouseleave')
        expect(wrapper.vm.$data.indexOfHoveredRow).to.equal(null)
    })
})
