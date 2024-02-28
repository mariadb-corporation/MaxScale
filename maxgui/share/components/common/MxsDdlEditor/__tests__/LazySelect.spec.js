/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import LazySelect from '@share/components/common/MxsDdlEditor/LazySelect'
import { lodash } from '@share/utils/helpers'

const itemsStub = ['a', 'b']

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: LazySelect,
                attrs: {
                    value: true,
                    height: 28,
                    items: itemsStub,
                    disabled: false,
                    required: false,
                    name: 'select-input',
                },
            },
            opts
        )
    )

describe('lazy-select', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-lazy-input`, () => {
            wrapper = mountFactory()
            const {
                value,
                inputValue,
                height,
                disabled,
                required,
                type,
                getInputRef,
                name,
            } = wrapper.findComponent({ name: 'mxs-lazy-input' }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.isInputShown)
            expect(inputValue).to.be.eql(wrapper.vm.inputValue)
            expect(type).to.be.eql('select')
            expect(height).to.be.eql(wrapper.vm.$attrs.height)
            expect(disabled).to.be.eql(wrapper.vm.$attrs.disabled)
            expect(required).to.be.eql(wrapper.vm.$attrs.required)
            expect(name).to.be.eql(wrapper.vm.$attrs.name)
            expect(getInputRef).to.be.a('function')
        })
        it(`Should pass accurate data to v-select`, () => {
            wrapper = mountFactory({ data: () => ({ isInputShown: true }) })
            const {
                value,
                items,
                disabled,
                height,
                hideDetails,
                cacheItems,
            } = wrapper.findComponent({ name: 'v-select' }).vm.$props
            expect(value).to.be.eql(wrapper.vm.inputValue)
            expect(items).to.be.eql(wrapper.vm.$attrs.items)
            expect(disabled).to.be.eql(wrapper.vm.$attrs.disabled)
            expect(height).to.be.eql(wrapper.vm.$attrs.height)
            expect(hideDetails).to.be.true
            expect(cacheItems).to.be.true
        })
    })

    describe(`Computed properties`, () => {
        it(`Should return accurate value for inputValue`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$attrs.value)
        })

        it(`Should emit on-input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.inputValue = itemsStub[1]
            expect(wrapper.emitted('on-input')[0]).to.be.eql([itemsStub[1]])
        })
    })
})
