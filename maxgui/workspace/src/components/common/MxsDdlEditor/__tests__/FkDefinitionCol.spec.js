/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import FkDefinitionCol from '@wsSrc/components/common/MxsDdlEditor/FkDefinitionCol'
import { lodash } from '@share/utils/helpers'
import { FK_EDITOR_ATTRS } from '@wsSrc/store/config'

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
const refTargetsStub = [
    { id: 'tbl_750b4681-3b5b-11ee-a3ad-dfd43862371d', text: '`company`.`employees`' },
    { id: 'tbl_0dbaec71-3b5f-11ee-91b9-f792d1167277', text: '`company`.`department`' },
]

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: FkDefinitionCol,
                propsData: {
                    data: { field: FK_EDITOR_ATTRS.NAME, value: 'employees_ibfk_0', rowIdx: 0 },
                    height: 28,
                    referencingColOptions: referencingColOptsStub,
                    refTargets: refTargetsStub,
                    refColOpts: refColOptsStub,
                },
            },
            opts
        )
    )

describe('fk-definition-col', () => {
    let wrapper
    const dropdownFields = Object.values(FK_EDITOR_ATTRS).filter(
        field => field !== FK_EDITOR_ATTRS.ID && field !== FK_EDITOR_ATTRS.NAME
    )
    describe(`Child component's data communication tests`, () => {
        Object.values(FK_EDITOR_ATTRS).forEach(field => {
            if (field !== FK_EDITOR_ATTRS.ID) {
                const componentName =
                    field === FK_EDITOR_ATTRS.NAME ? 'mxs-debounced-field' : 'v-select'
                it(`Should render ${componentName} for ${field} field`, () => {
                    wrapper = mountFactory({
                        propsData: { data: { field: field, value: 'employees_ibfk_0', rowIdx: 0 } },
                    })
                    const component = wrapper.findComponent({ name: componentName })
                    expect(component.exists()).to.be.true
                })
            }
        })

        it(`Should enable multiple for COLS and REF_COLS fields`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.data.value)
        })

        it(`Should pass isColumnField to v-select multiple props`, () => {
            wrapper = mountFactory({
                propsData: {
                    data: { field: FK_EDITOR_ATTRS.COLS, value: 'employees_ibfk_0', rowIdx: 0 },
                },
            })
            const component = wrapper.findComponent({ name: 'v-select' })
            expect(component.vm.$props.multiple).to.be.eql(wrapper.vm.isColumnField)
        })
    })

    describe(`Computed properties tests`, () => {
        it(`Should return accurate value for inputValue`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.data.value)
        })

        it(`Should emit on-input event`, () => {
            wrapper = mountFactory()
            wrapper.vm.inputValue = ''
            expect(wrapper.emitted('on-input')[0]).to.be.eql([
                { ...wrapper.vm.$props.data, value: '' },
            ])
        })

        dropdownFields.forEach(field => {
            const expectValue = field === FK_EDITOR_ATTRS.COLS || field === FK_EDITOR_ATTRS.REF_COLS
            it(`Should return accurate value for isColumnField when field is ${field}`, () => {
                wrapper = mountFactory({
                    propsData: { data: { field: field, value: 'employees_ibfk_0', rowIdx: 0 } },
                })
                expect(wrapper.vm.isColumnField).to.be.eql(expectValue)
            })
        })
    })
})
