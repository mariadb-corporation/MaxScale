/*
 * Copyright (c) 2023 MariaDB plc
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
import WkeSidebar from '@wkeComps/WkeSidebar.vue'

describe('wke-sidebar', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: WkeSidebar,
            propsData: { value: false, title: 'Schemas' },
        })
    })
    it('Should hide the title when sidebar is collapsed', async () => {
        await wrapper.setProps({ value: true })
        const titleElement = wrapper.find('.sidebar-toolbar__title')
        expect(titleElement.exists()).to.be.false
    })

    it('Emits an input event with the new value when the collapse button is clicked', () => {
        wrapper.find('.toggle-sidebar').trigger('click')
        expect(wrapper.emitted()).to.have.property('input')
        expect(wrapper.emitted().input[0]).to.be.eql([true])
    })

    it('should not render the collapse-btn-prepend slot when collapsed', () => {
        wrapper = mount({
            shallow: false,
            component: WkeSidebar,
            propsData: { value: true, title: 'Schemas' },
            slots: {
                'collapse-btn-prepend': '<button class="btn-prepend">Prepend</button>',
            },
        })
        expect(wrapper.find('.btn-prepend').exists()).to.be.false
    })
})
