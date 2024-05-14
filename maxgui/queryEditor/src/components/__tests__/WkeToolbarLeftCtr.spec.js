/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import WkeToolbarLeftCtr from '../WkeToolbarLeftCtr.vue'

const mountFactory = opts => mount({ shallow: false, component: WkeToolbarLeftCtr, ...opts })

describe(`wke-toolbar-left-ctr`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Should emit get-total-width evt in the next tick after component is mounted', () => {
        wrapper.vm.$nextTick(() => {
            expect(wrapper.emitted()).to.have.property('get-total-width')
        })
    })

    it(`Should call addNewWs action`, async () => {
        let isCalled = false
        let wrapper = mountFactory({
            computed: { isAddWkeDisabled: () => false },
            methods: { addNewWs: () => (isCalled = true) },
        })
        await wrapper.find('.add-wke-btn').trigger('click')
        expect(isCalled).to.be.true
    })
})
