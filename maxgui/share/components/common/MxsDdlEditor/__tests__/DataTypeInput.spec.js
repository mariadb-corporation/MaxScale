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
import DataTypeInput from '@share/components/common/MxsDdlEditor/DataTypeInput'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: DataTypeInput,
                propsData: {
                    value: true,
                    height: 28,
                    items: [],
                },
            },
            opts
        )
    )

describe('data-type-input', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-lazy-input`, () => {
            wrapper = mountFactory()
            const {
                value,
                inputValue,
                height,
                type,
                required,
                getInputRef,
                error,
                name,
            } = wrapper.findComponent({ name: 'mxs-lazy-input' }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.isInputShown)
            expect(inputValue).to.be.eql(wrapper.vm.inputValue)
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(type).to.be.eql('select')
            expect(required).to.be.true
            expect(error).to.be.eql(wrapper.vm.$data.error)
            expect(getInputRef).to.be.a('function')
            expect(name).to.be.eql('data-type')
        })
        it(`Should pass accurate data to v-combobox`, () => {
            wrapper = mountFactory({ data: () => ({ isInputShown: true }) })
            const {
                value,
                items,
                height,
                hideDetails,
                cacheItems,
                itemText,
                itemValue,
                error,
                rules,
            } = wrapper.findComponent({ name: 'v-combobox' }).vm.$props
            expect(value).to.be.eql(wrapper.vm.inputValue)
            expect(items).to.be.eql(wrapper.vm.$props.items)
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(error).to.be.eql(wrapper.vm.$data.error)
            expect(itemText).to.be.eql('value')
            expect(itemValue).to.be.eql('value')
            expect(hideDetails).to.be.true
            expect(cacheItems).to.be.true
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
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit on-input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.inputValue = 'int'
            expect(wrapper.emitted('on-input')[0]).to.be.eql(['int'])
        })
    })
})
