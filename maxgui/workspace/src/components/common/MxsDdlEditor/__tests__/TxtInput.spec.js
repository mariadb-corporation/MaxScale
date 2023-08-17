/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import TxtInput from '@wsSrc/components/common/MxsDdlEditor/TxtInput'
import { COL_ATTRS } from '@wsSrc/store/config'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: TxtInput,
                propsData: {
                    value: 'id',
                    field: COL_ATTRS.NAME,
                    height: 28,
                },
            },
            opts
        )
    )

describe('txt-input', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-lazy-input`, () => {
            wrapper = mountFactory()
            const {
                value,
                inputValue,
                height,
                required,
                error,
                getInputRef,
            } = wrapper.findComponent({ name: 'mxs-lazy-input' }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.isInputShown)
            expect(inputValue).to.be.eql(wrapper.vm.inputValue)
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(required).to.be.eql(wrapper.vm.isRequired)
            expect(error).to.be.eql(wrapper.vm.$data.error)
            expect(getInputRef).to.be.a('function')
        })
        it(`Should pass accurate data to mxs-debounced-field`, () => {
            wrapper = mountFactory({ data: () => ({ isInputShown: true }) })
            const {
                value,
                height,
                ['hide-details']: hideDetails,
                error,
                required,
                rules,
            } = wrapper.findComponent({
                name: 'mxs-debounced-field',
            }).vm.$attrs
            expect(value).to.be.eql(wrapper.vm.inputValue)
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(hideDetails).to.be.eql('')
            expect(error).to.be.eql(wrapper.vm.$data.error)
            expect(required).to.be.eql(wrapper.vm.isRequired)
            expect(rules).to.be.an('array')
            expect(rules.length).to.be.eql(1)
            expect(rules[0]).to.be.a('function')
            const fnRes = rules[0]('')
            expect(fnRes).to.be.a('boolean')
        })
    })

    describe(`Computed properties`, () => {
        it(`Should return accurate value for isRequired`, () => {
            wrapper = mountFactory({ propsData: { field: COL_ATTRS.COMMENT } })
            expect(wrapper.vm.isRequired).to.be.false
        })

        it(`Should return accurate value for inputValue`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit on-input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.inputValue = 'department-id'
            expect(wrapper.emitted('on-input')[0]).to.be.eql(['department-id'])
        })
    })
})
