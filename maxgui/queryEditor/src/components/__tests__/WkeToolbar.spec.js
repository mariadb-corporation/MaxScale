/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import WkeToolbar from '../WkeToolbar.vue'

const mountFactory = opts => mount({ shallow: false, component: WkeToolbar, ...opts })

describe(`wke-toolbar - mounted hook and child component's interaction tests`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Should emit get-total-btn-width evt', () => {
        expect(wrapper.emitted()).to.have.property('get-total-btn-width')
    })
    const childComps = ['wke-toolbar-left-ctr', 'wke-toolbar-right']
    childComps.forEach(name => {
        it(`Should render ${name}`, () => {
            const comp = wrapper.findComponent({ name })
            expect(comp.exists()).to.be.true
        })
    })
})
