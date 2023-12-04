/*
 * Copyright (c) 2023 MariaDB plc
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
import MxsTimeoutInput from '@wsSrc/components/common/MxsTimeoutInput'

describe(`mxs-timeout-input`, () => {
    let wrapper

    it(`Should pass accurate data to mxs-label-field`, () => {
        wrapper = mount({ shallow: false, component: MxsTimeoutInput, attrs: { value: 100 } })
        const {
            $props: { label },
            $attrs: { value, name, type, required },
        } = wrapper.findComponent({
            name: 'mxs-label-field',
        }).vm
        expect(value).to.equal(100)
        expect(label).to.equal(wrapper.vm.$mxs_t('timeout'))
        expect(name).to.equal('timeout')
        expect(type).to.equal('number')
        expect(required).to.be.true
    })
})
