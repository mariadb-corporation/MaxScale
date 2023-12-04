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
import CharsetCollateInput from '@wsSrc/components/common/MxsDdlEditor/CharsetCollateInput'
import { lodash } from '@share/utils/helpers'
import { COL_ATTRS } from '@wsSrc/store/config'
import {
    rowDataStub,
    charsetCollationMapStub,
} from '@wsSrc/components/common/MxsDdlEditor/__tests__/stubData'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: CharsetCollateInput,
                propsData: {
                    value: true,
                    rowData: rowDataStub,
                    field: COL_ATTRS.CHARSET,
                    height: 28,
                    charsetCollationMap: charsetCollationMapStub,
                },
            },
            opts
        )
    )

describe('charset-collate-input', () => {
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
                type,
                getInputRef,
            } = wrapper.findComponent({ name: 'mxs-lazy-input' }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.isInputShown)
            expect(inputValue).to.be.eql(wrapper.vm.inputValue)
            expect(type).to.be.eql('select')
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(name).to.be.eql(wrapper.vm.name)
            expect(disabled).to.be.eql(wrapper.vm.isDisabled)
            expect(getInputRef).to.be.a('function')
        })
        it(`Should pass accurate data to charset-collate-select`, () => {
            wrapper = mountFactory({ data: () => ({ isInputShown: true }) })

            const {
                $attrs: { value, items, disabled, height },
                $props: { defItem },
            } = wrapper.findComponent({
                name: 'charset-collate-select',
            }).vm
            expect(value).to.be.eql(wrapper.vm.inputValue)
            expect(items).to.be.eql(Object.keys(charsetCollationMapStub))
            expect(disabled).to.be.eql(wrapper.vm.isDisabled)
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(defItem).to.be.eql(wrapper.vm.$props.defTblCharset)
        })
    })

    describe(`Computed properties and method tests`, () => {
        it(`isDisabled should return true if columnType includes 'NATIONAL'`, () => {
            wrapper = mountFactory({
                computed: {
                    columnType: () => 'NATIONAL CHAR',
                },
            })
            expect(wrapper.vm.isDisabled).to.be.true
        })
        it(`Should return accurate value for inputValue`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit on-input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.inputValue = 'dec8'
            expect(wrapper.emitted('on-input')[0]).to.be.eql(['dec8'])
        })
    })
})
