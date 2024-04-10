/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import MxsLazyInput from '@share/components/common/MxsLazyInput'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: MxsLazyInput,
                propsData: {
                    value: false,
                    height: 28,
                    getInputRef: () => null,
                    type: 'text',
                    name: 'test-input',
                },
            },
            opts
        )
    )

describe('mxs-lazy-input', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        const inputTypes = ['text', 'select', 'checkbox']
        inputTypes.forEach(type => {
            it(`Should render readonly ${type} input when value props is false`, () => {
                wrapper = mountFactory({ propsData: { type } })
                switch (type) {
                    case 'text':
                    case 'select': {
                        const input = wrapper.find('input[type="text"]')
                        expect(input.exists()).to.be.true
                        expect(input.attributes()).to.include.keys(
                            'type',
                            'placeholder',
                            'name',
                            'autocomplete'
                        )
                        if (type === 'select')
                            expect(wrapper.findComponent({ name: 'v-icon' }).exists()).to.be.true
                        break
                    }
                    case 'checkbox':
                        expect(wrapper.findComponent({ name: 'v-simple-checkbox' }).exists()).to.be
                            .true
                        break
                }
            })
        })

        it(`Should render the default slot when value props is true`, () => {
            wrapper = mountFactory({
                propsData: { value: true },
                slots: {
                    default: '<div id="slot-content"/>',
                },
            })
            expect(wrapper.find('#slot-content').exists()).to.be.true
        })

        const expectedClasses = {
            disabled: 'lazy-input--disabled',
            error: 'lazy-input--error',
        }
        Object.keys(expectedClasses).forEach(props => {
            const className = expectedClasses[props]
            it(`Should add ${className} class when ${props} props is true`, () => {
                wrapper = mountFactory({ propsData: { [props]: true } })
                expect(wrapper.vm.readonlyInputClass).to.include(className)
            })
        })
    })

    describe(`Computed properties tests`, () => {
        it(`Should return accurate value for isError`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.isError).to.be.eql(wrapper.vm.$props.error)
        })

        it(`Should emit update:error event`, () => {
            wrapper = mountFactory()
            wrapper.vm.isError = true
            expect(wrapper.emitted('update:error')[0]).to.be.eql([true])
        })

        it(`Should return accurate value for isInputShown`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.isInputShown).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.isInputShown = true
            expect(wrapper.emitted('input')[0]).to.be.eql([true])
        })
    })

    describe('Watchers tests', () => {
        it('Should emit input event when isError is true', async () => {
            wrapper = mountFactory()
            await wrapper.setProps({ error: true })
            expect(wrapper.emitted('input')[0]).to.deep.equal([true])
        })
    })
})
