/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import FkColFieldInput from '@share/components/common/MxsDdlEditor/FkColFieldInput'
import { lodash } from '@share/utils/helpers'
import { FK_EDITOR_ATTRS } from '@wsSrc/constants'

const referencingColOptsStub = [
    {
        id: 'col_750b1f70-3b5b-11ee-a3ad-dfd43862371d',
        text: 'id',
        type: 'int(11)',
        disabled: false,
    },
    {
        id: 'col_750b1f71-3b5b-11ee-a3ad-dfd43862371d',
        text: 'department_id',
        type: 'int(11)',
        disabled: false,
    },
]
const refColOptsStub = [
    {
        id: 'col_f2404150-3b6f-11ee-9c95-7dfe6062fdca',
        text: 'id',
        type: 'int(11)',
        disabled: false,
    },
    {
        id: 'col_f2404151-3b6f-11ee-9c95-7dfe6062fdca',
        text: 'name',
        type: 'varchar(100)',
        disabled: false,
    },
]

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: FkColFieldInput,
                propsData: {
                    value: [
                        'col_41edfb20-3dbe-11ee-89d0-3d05b76780b9',
                        'col_41edfb21-3dbe-11ee-89d0-3d05b76780b9',
                    ],
                    field: FK_EDITOR_ATTRS.COLS,
                    height: 28,
                    referencingColOptions: referencingColOptsStub,
                    refColOpts: refColOptsStub,
                },
            },
            opts
        )
    )

describe('fk-col-field-input', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to lazy-select`, () => {
            wrapper = mountFactory()
            const {
                $attrs: {
                    value,
                    height,
                    name,
                    items,
                    ['item-text']: itemText,
                    ['item-value']: itemValue,
                    multiple,
                    required,
                    rules,
                },
                $props: { selectionText },
            } = wrapper.findComponent({
                name: 'lazy-select',
            }).vm
            expect(value).to.be.eql(wrapper.vm.inputValue)
            expect(height).to.be.eql(wrapper.vm.$props.height)
            expect(name).to.be.eql(wrapper.vm.$props.field)
            expect(items).to.be.eql(wrapper.vm.items)
            expect(itemText).to.be.eql('text')
            expect(itemValue).to.be.eql('id')
            expect(multiple).to.be.true
            expect(required).to.be.true
            expect(rules).to.be.an('array')
            expect(selectionText).to.be.eql(wrapper.vm.selectionText)
        })
    })

    describe(`Computed properties tests`, () => {
        it(`Should return accurate value for inputValue`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit on-input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.inputValue = ''
            expect(wrapper.emitted('on-input')[0]).to.be.eql([''])
        })
    })
})
