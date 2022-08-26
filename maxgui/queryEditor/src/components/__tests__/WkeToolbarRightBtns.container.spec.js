/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import WkeToolbarRightBtns from '../WkeToolbarRightBtns.container.vue'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: WkeToolbarRightBtns,
        stubs: {
            'readonly-sql-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

describe(`wke-toolbar-left-btns-ctr`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Should emit get-total-width evt in the next tick after component is mounted', () => {
        wrapper.vm.$nextTick(() => {
            expect(wrapper.emitted()).to.have.property('get-total-width')
        })
    })
    it('Should pass accurate data to query-cnf-dlg-ctr via attrs', () => {
        const cnfDlg = wrapper.findComponent({ name: 'query-cnf-dlg-ctr' })
        expect(cnfDlg.vm.$attrs.value).to.be.equals(wrapper.vm.queryConfigDialog)
    })
    it(`Should call SET_FULLSCREEN mutation`, () => {
        let wrapper = mountFactory()
        const spy = sinon.spy(wrapper.vm, 'SET_FULLSCREEN')
        const btn = wrapper.find('.min-max-btn')
        btn.trigger('click')
        spy.should.have.been.calledOnce
    })

    it(`Should popup query setting dialog`, () => {
        let wrapper = mountFactory()
        expect(wrapper.vm.queryConfigDialog).to.be.false
        wrapper.find('.query-setting-btn').trigger('click')
        expect(wrapper.vm.queryConfigDialog).to.be.true
    })
})
