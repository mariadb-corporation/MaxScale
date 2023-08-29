/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import MinMaxBtnCtr from '../MinMaxBtnCtr.vue'

const mountFactory = opts => mount({ shallow: false, component: MinMaxBtnCtr, ...opts })

describe(`min-max-btn-ctr`, () => {
    it(`Should call SET_FULLSCREEN mutation`, () => {
        let wrapper = mountFactory()
        const spy = sinon.spy(wrapper.vm, 'SET_FULLSCREEN')
        const btn = wrapper.find('.min-max-btn')
        btn.trigger('click')
        spy.should.have.been.calledOnce
    })
})
