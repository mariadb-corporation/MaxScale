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
import LazyTextField from '@share/components/common/MxsDdlEditor/LazyTextField'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: LazyTextField,
                attrs: {
                    value: 'id',
                    height: 28,
                    disabled: false,
                    required: false,
                    name: 'test-input',
                },
            },
            opts
        )
    )

describe('lazy-text-field', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-lazy-input`, () => {
            wrapper = mountFactory()
            const {
                value,
                inputValue,
                height,
                name,
                disabled,
                required,
                error,
                getInputRef,
            } = wrapper.findComponent({ name: 'mxs-lazy-input' }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.isInputShown)
            expect(inputValue).to.be.eql(wrapper.vm.inputValue)
            expect(name).to.be.eql(wrapper.vm.$attrs.name)
            expect(height).to.be.eql(wrapper.vm.$attrs.height)
            expect(disabled).to.be.eql(wrapper.vm.$attrs.disabled)
            expect(required).to.be.eql(wrapper.vm.$attrs.required)
            expect(error).to.be.eql(wrapper.vm.$data.error)
            expect(getInputRef).to.be.a('function')
        })
        it(`Should pass accurate data to mxs-debounced-field`, () => {
            wrapper = mountFactory({ data: () => ({ isInputShown: true }) })
            const {
                value,
                height,
                autocomplete,
                ['hide-details']: hideDetails,
                error,
                disabled,
                required,
                rules,
                name,
            } = wrapper.findComponent({
                name: 'mxs-debounced-field',
            }).vm.$attrs
            expect(value).to.be.eql(wrapper.vm.inputValue)
            expect(height).to.be.eql(wrapper.vm.$attrs.height)
            expect(autocomplete).to.be.eql('off')
            expect(hideDetails).to.be.eql('')
            expect(name).to.be.eql(wrapper.vm.$attrs.name)
            expect(error).to.be.eql(wrapper.vm.$data.error)
            expect(disabled).to.be.eql(wrapper.vm.$attrs.disabled)
            expect(required).to.be.eql(wrapper.vm.$attrs.required)
            expect(rules).to.be.an('array')
            expect(rules.length).to.be.eql(1)
            expect(rules[0]).to.be.a('function')
            const fnRes = rules[0]('')
            expect(fnRes).to.be.a('boolean')
        })
    })

    describe(`Computed properties`, () => {
        it(`Should return accurate value for inputValue`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$attrs.value)
        })

        it(`Should emit on-input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.inputValue = 'department-id'
            expect(wrapper.emitted('on-input')[0]).to.be.eql(['department-id'])
        })
    })
})
