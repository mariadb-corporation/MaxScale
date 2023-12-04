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
import BoolInput from '@wsSrc/components/common/MxsDdlEditor/BoolInput'
import { COL_ATTRS } from '@wsSrc/store/config'
import { lodash } from '@share/utils/helpers'
import { rowDataStub } from '@wsSrc/components/common/MxsDdlEditor/__tests__/stubData'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: BoolInput,
                propsData: {
                    value: true,
                    height: 28,
                    rowData: rowDataStub,
                    field: COL_ATTRS.PK,
                },
            },
            opts
        )
    )

describe('bool-input', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-lazy-input`, () => {
            wrapper = mountFactory()
            const {
                value,
                inputValue,
                type,
                height,
                name,
                disabled,
                getInputRef,
            } = wrapper.findComponent({ name: 'mxs-lazy-input' }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.isInputShown)
            expect(inputValue).to.be.eql(wrapper.vm.inputValue)
            expect(type).to.be.eql('checkbox')
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(name).to.be.eql(wrapper.vm.$props.field)
            expect(disabled).to.be.eql(wrapper.vm.isDisabled)
            expect(getInputRef).to.be.a('function')
        })
        it(`Should pass accurate data to v-checkbox`, () => {
            wrapper = mountFactory({ data: () => ({ isInputShown: true }) })

            const { inputValue, hideDetails, disabled } = wrapper.findComponent({
                name: 'v-checkbox',
            }).vm.$props
            expect(inputValue).to.be.eql(wrapper.vm.inputValue)
            expect(disabled).to.be.eql(wrapper.vm.isDisabled)
            expect(hideDetails).to.be.true
        })
    })

    describe(`Computed properties tests`, () => {
        it(`colData should have expected properties`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.colData).to.have.all.keys('type', 'isPK', 'isAI', 'isGenerated')
        })
        it(`Should return accurate value for inputValue`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit on-input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.inputValue = false
            expect(wrapper.emitted('on-input')[0]).to.be.eql([false])
        })
    })
})
