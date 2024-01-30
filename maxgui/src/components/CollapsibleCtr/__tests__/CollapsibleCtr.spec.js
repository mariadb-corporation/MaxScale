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
import CollapsibleCtr from '@rootSrc/components/CollapsibleCtr'

describe('CollapsibleCtr.vue', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: CollapsibleCtr,
            propsData: { title: 'CollapsibleCtr title' },
        })
    })

    it('Should hide content when the toggle button is clicked', async () => {
        await wrapper.find('[data-test="toggle-btn"]').trigger('click')
        expect(wrapper.vm.$data.isVisible).to.be.false
        expect(wrapper.find('[data-test="content"]').attributes().style).to.equal('display: none;')
    })

    const slots = ['title-append', 'header-right', 'default']
    slots.forEach(slot =>
        it(`Should render ${slot} slot `, () => {
            const slotContent = `<div class="${slot}"></div>`
            wrapper = mount({
                shallow: false,
                component: CollapsibleCtr,
                slots: { [slot]: slotContent },
            })

            expect(wrapper.find(`.${slot}`).html()).to.be.equal(slotContent)
        })
    )
})
