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
import GeneratedInput from '@wsSrc/components/common/MxsDdlEditor/GeneratedInput'
import { lodash } from '@share/utils/helpers'
import { GENERATED_TYPES } from '@wsSrc/store/config'
import { rowDataStub } from '@wsSrc/components/common/MxsDdlEditor/__tests__/stubData'

const itemsStub = Object.values(GENERATED_TYPES)
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: GeneratedInput,
                propsData: {
                    value: true,
                    rowData: rowDataStub,
                    height: 28,
                    items: itemsStub,
                },
            },
            opts
        )
    )

describe('generated-input', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-lazy-input`, () => {
            wrapper = mountFactory()
            const {
                value,
                inputValue,
                height,
                disabled,
                type,
                getInputRef,
            } = wrapper.findComponent({ name: 'mxs-lazy-input' }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.isInputShown)
            expect(inputValue).to.be.eql(wrapper.vm.inputValue)
            expect(type).to.be.eql('select')
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(disabled).to.be.eql(wrapper.vm.isDisabled)
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
            } = wrapper.findComponent({ name: 'v-select' }).vm
            expect(value).to.be.eql(wrapper.vm.inputValue)
            expect(items).to.be.eql(itemsStub)
            expect(disabled).to.be.eql(wrapper.vm.isDisabled)
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(hideDetails).to.be.true
            expect(cacheItems).to.be.true
        })
    })

    describe(`Computed properties`, () => {
        it(`isDisabled should return true if isPK true`, () => {
            wrapper = mountFactory({ computed: { isPK: () => true } })
            expect(wrapper.vm.isDisabled).to.be.true
        })
        it(`Should return accurate value for inputValue`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit on-input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.inputValue = GENERATED_TYPES.STORED
            expect(wrapper.emitted('on-input')[0]).to.be.eql([GENERATED_TYPES.STORED])
        })
    })
})
