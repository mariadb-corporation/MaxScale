/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import QueryCnfGearBtn from '../QueryCnfGearBtn.vue'

const mountFactory = opts => mount({ shallow: false, component: QueryCnfGearBtn, ...opts })

describe(`query-cnf-gear-btn`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Should pass accurate data to query-cnf-dlg-ctr via attrs', () => {
        const cnfDlg = wrapper.findComponent({ name: 'query-cnf-dlg-ctr' })
        expect(cnfDlg.vm.$attrs.value).to.be.equals(wrapper.vm.queryConfigDialog)
    })
    it(`Should popup query setting dialog`, () => {
        expect(wrapper.vm.queryConfigDialog).to.be.false
        wrapper.find('.query-setting-btn').trigger('click')
        expect(wrapper.vm.queryConfigDialog).to.be.true
    })
})
