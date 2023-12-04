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
import EtlDestConn from '@wkeComps/DataMigration/EtlDestConn'

const valueStub = { user: '', password: '', timeout: 30, target: '' }
const allServersStub = [
    { id: 'server_0', type: 'servers' },
    { id: 'server_1', type: 'servers' },
    { id: 'server_2', type: 'servers' },
]
describe('EtlDestConn', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: true,
            component: EtlDestConn,
            propsData: { value: valueStub, destTargetType: 'servers', allServers: allServersStub },
        })
    })
    afterEach(() => sinon.restore())

    it('Should pass accurate data to v-select', () => {
        const { value, items, itemText, itemValue, hideDetails } = wrapper.findComponent({
            name: 'v-select',
        }).vm.$props
        expect(value).to.equal(wrapper.vm.$data.dest.target)
        expect(items).to.eql(wrapper.vm.$props.allServers)
        expect(itemText).to.equal('id')
        expect(itemValue).to.equal('id')
        expect(hideDetails).to.equal('auto')
    })

    const otherChildComponents = [
        { name: 'mxs-timeout-input', fieldName: 'timeout' },
        { name: 'mxs-uid-input', fieldName: 'user' },
        { name: 'mxs-pwd-input', fieldName: 'password' },
    ]
    otherChildComponents.forEach(({ name, fieldName }) => {
        it(`Should pass accurate data to ${name}`, () => {
            const { value } = wrapper.findComponent({ name }).vm.$attrs
            expect(value).to.equal(wrapper.vm.$data.dest[fieldName])
        })
    })

    it('Should immediately emit input event', () => {
        expect(wrapper.emitted('input')[0][0]).to.eql(valueStub)
    })
})
