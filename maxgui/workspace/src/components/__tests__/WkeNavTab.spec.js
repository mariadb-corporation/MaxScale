/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import WkeNavTab from '../WkeNavTab.vue'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: WkeNavTab,
        propsData: {
            worksheet: dummyWke,
        },
        ...opts,
    })

const dummyWke = {
    id: '71cb4820-76d6-11ed-b6c2-dfe0423852da',
    active_query_tab_id: '71cb4821-76d6-11ed-b6c2-dfe0423852da',
    name: 'WORKSHEET',
}
describe('wke-nav-tab', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mountFactory({
            computed: {
                wkeId: () => dummyWke.id,
                wkeConn: () => ({}),
                isWkeLoadingQueryResult: () => false,
                totalWorksheets: () => 1,
            },
        })
    })

    it('Should show delete worksheet button', () => {
        wrapper = mountFactory({ computed: { totalWorksheets: () => 2 } })
        expect(wrapper.find('.del-tab-btn').exists()).to.be.equal(true)
    })

    it('Should not show delete worksheet button if totalWorksheets <= 1', () => {
        expect(wrapper.find('.del-tab-btn').exists()).to.be.equal(false)
    })

    it('Should not show a tooltip when hovering a worksheet tab has no connection', () => {
        expect(wrapper.findComponent({ name: 'v-tooltip' }).vm.$props.disabled).to.be.true
    })

    it('Should show a tooltip when hovering a worksheet tab has a connection', () => {
        wrapper = mountFactory({
            computed: {
                wkeConn: () => ({
                    id: '0',
                    name: 'server_0',
                    type: 'servers',
                }),
            },
        })
        expect(wrapper.findComponent({ name: 'v-tooltip' }).vm.$props.disabled).to.be.false
    })
})
