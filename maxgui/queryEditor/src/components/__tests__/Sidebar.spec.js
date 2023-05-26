/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import Sidebar from '../Sidebar.vue'

const mountFactory = opts =>
    mount({
        shallow: true,
        component: Sidebar,
        ...opts,
    })

describe('sidebar', () => {
    let wrapper
    describe(`computed properties tests`, () => {
        it(`Should return accurate value for reloadDisabled`, async () => {
            // has connection
            wrapper = mountFactory({ propsData: { hasConn: true } })
            expect(wrapper.vm.reloadDisabled).to.be.false
            // have no connection and still loading for data
            await wrapper.setProps({ hasConn: false, isLoading: true })
            expect(wrapper.vm.reloadDisabled).to.be.true
        })
    })

    describe(`Button tests`, () => {
        it(`Should disable reload-schemas button`, () => {
            wrapper = mountFactory({
                shallow: false,
                computed: { reloadDisabled: () => true },
            })
            expect(wrapper.find('.reload-schemas').attributes().disabled).to.be.equals('disabled')
        })
        it(`Should disable filter-objects input`, () => {
            wrapper = mountFactory({
                shallow: false,
                computed: { reloadDisabled: () => true },
            })
            expect(
                wrapper
                    .find('.filter-objects')
                    .find('input')
                    .attributes().disabled
            ).to.be.equals('disabled')
        })

        const evts = ['reload-schemas', 'toggle-sidebar']
        evts.forEach(evt => {
            it(`Should emit ${evt} when ${evt} button is clicked`, async () => {
                wrapper = mountFactory({
                    shallow: false,
                    computed: { reloadDisabled: () => false },
                })
                await wrapper.find(`.${evt}`).trigger('click')
                expect(wrapper.emitted()).to.have.property(evt)
            })
        })
    })
})
