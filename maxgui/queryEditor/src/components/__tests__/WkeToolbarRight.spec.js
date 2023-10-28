/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import WkeToolbarRight from '../WkeToolbarRight.vue'

const mountFactory = opts => mount({ shallow: false, component: WkeToolbarRight, ...opts })

describe(`wke-toolbar-right`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Should emit get-total-width evt in the next tick after component is mounted', () => {
        wrapper.vm.$nextTick(() => {
            expect(wrapper.emitted()).to.have.property('get-total-width')
        })
    })
    const childComps = ['query-cnf-gear-btn', 'min-max-btn-ctr']
    childComps.forEach(name => {
        it(`Should render ${name}`, () => {
            const comp = wrapper.findComponent({ name })
            expect(comp.exists()).to.be.true
        })
    })
})
