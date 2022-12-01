/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import WkeNavCtr from '../WkeNavCtr.vue'

const mountFactory = opts => mount({ shallow: false, component: WkeNavCtr, ...opts })

describe('wke-nav-ctr', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mountFactory()
    })

    it('Should not show delete worksheet button when getAllWorksheets length <= 1', () => {
        expect(wrapper.vm.getAllWorksheets.length).to.be.equals(1)
        expect(wrapper.find('.del-tab-btn').exists()).to.be.equal(false)
    })

    it('Should show delete worksheet button when getAllWorksheets length > 1', () => {
        expect(wrapper.vm.getAllWorksheets.length).to.be.equals(1)
        // stubs getAllWorksheets
        wrapper = mountFactory({
            computed: {
                getAllWorksheets: () => [
                    ...wrapper.vm.getAllWorksheets,
                    { ...wrapper.vm.getAllWorksheets[0], id: 'dummy_1234' },
                ],
            },
        })
        expect(wrapper.vm.getAllWorksheets.length).to.be.equals(2)
        expect(wrapper.find('.del-tab-btn').exists()).to.be.equal(true)
    })

    it('Should not show a tooltip when hovering a worksheet tab has no connection', () => {
        expect(wrapper.findComponent({ name: 'v-tooltip' }).vm.$props.disabled).to.be.true
    })

    it('Should show a tooltip when hovering a worksheet tab has a connection', () => {
        wrapper = mountFactory({
            computed: {
                getWkeConnByWkeId: () => () => ({
                    id: '0',
                    name: 'server_0',
                    type: 'servers',
                }),
            },
        })
        expect(wrapper.findComponent({ name: 'v-tooltip' }).vm.$props.disabled).to.be.false
    })
})
