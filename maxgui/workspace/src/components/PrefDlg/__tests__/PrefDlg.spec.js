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
import PrefDlg from '@wsComps/PrefDlg'
import { addDaysToNow } from '@wsSrc/utils/helpers'

const mountFactory = opts =>
    mount({
        shallow: true,
        component: PrefDlg,
        attrs: {
            value: true, // open dialog
        },
        computed: {
            query_row_limit: () => 10000,
            query_confirm_flag: () => 1,
            query_history_expired_time: () => addDaysToNow(30),
            query_show_sys_schemas_flag: () => 1,
            tab_moves_focus: () => false,
            max_statements: () => 1000,
            identifier_auto_completion: () => true,
        },
        ...opts,
    })

/**
 * a mock to change a preferences value
 */
async function mockChangingConfig({ wrapper, key, value }) {
    await wrapper.setData({
        preferences: { ...wrapper.vm.$data.preferences, [key]: value },
    })
}

describe(`PrefDlg`, () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-dlg via props`, () => {
            let wrapper = mountFactory()
            const { value, title, onSave, lazyValidation, hasChanged } = wrapper.findComponent({
                name: 'mxs-dlg',
            }).vm.$props
            expect(value).to.be.equals(wrapper.vm.isOpened)
            expect(title).to.be.equals(wrapper.vm.$mxs_t('pref'))
            expect(onSave).to.be.equals(wrapper.vm.onSave)
            expect(lazyValidation).to.be.false
            expect(hasChanged).to.be.equals(wrapper.vm.hasChanged)
        })
    })

    describe(`Computed tests`, () => {
        beforeEach(() => {
            wrapper = mountFactory()
        })
        it(`prefFieldMap should have expected keys `, () => {
            const { QUERY_EDITOR, CONN } = wrapper.vm.PREF_TYPES
            expect(wrapper.vm.prefFieldMap).to.have.all.keys(QUERY_EDITOR, CONN)
        })

        it(`QUERY EDITOR preferences type should have expected keys `, () => {
            const { QUERY_EDITOR } = wrapper.vm.PREF_TYPES
            expect(wrapper.vm.prefFieldMap[QUERY_EDITOR]).to.have.all.keys(
                'positiveNumber',
                'boolean'
            )
        })

        const boolFields = [
            'query_confirm_flag',
            'query_show_sys_schemas_flag',
            'tab_moves_focus',
            'identifier_auto_completion',
        ]
        boolFields.forEach(field => {
            it(`persistedPref.${field} should be a boolean`, () => {
                expect(wrapper.vm.persistedPref[field]).to.be.a('boolean')
            })
        })
    })

    describe(`Tests after dialog is opened`, () => {
        beforeEach(() => {
            wrapper = mountFactory()
        })
        it(`Should deep copy persistedPref to preferences after dialog is opened `, () => {
            expect(wrapper.vm.$data.preferences).to.eqls(wrapper.vm.persistedPref)
        })
        it(`Should return accurate value for hasChanged`, async () => {
            await wrapper.setProps({ value: true }) // open dialog
            expect(wrapper.vm.hasChanged).to.be.false // no changes to form yet
            await mockChangingConfig({ wrapper, key: 'query_row_limit', value: 1 })
            expect(wrapper.vm.hasChanged).to.be.true
        })
    })
    describe('Row Limit', () => {
        beforeEach(() => {
            wrapper = mountFactory()
        })
        it(`Should pass accurate data to row-limit-ctr via attrs`, () => {
            const { height, 'hide-details': hideDetails } = wrapper.findComponent({
                name: 'row-limit-ctr',
            }).vm.$attrs
            expect(height).to.be.equals(36)
            expect(hideDetails).to.be.equals('auto')
        })
        it(`Should handle @change event emitted from row-limit-ctr`, () => {
            const newVal = 123
            wrapper.findComponent({ name: 'row-limit-ctr' }).vm.$emit('change', newVal)
            expect(wrapper.vm.$data.preferences.query_row_limit).to.be.eql(newVal)
        })
    })
})
