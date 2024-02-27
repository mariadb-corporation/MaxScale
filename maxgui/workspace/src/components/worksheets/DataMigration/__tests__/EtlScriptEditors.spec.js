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
import EtlScriptEditors from '@wkeComps/DataMigration/EtlScriptEditors'
import { lodash } from '@share/utils/helpers'

const valueStub = {
    create: 'create script',
    insert: 'insert script',
    select: 'select script',
}
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: EtlScriptEditors,
                propsData: { value: valueStub, hasChanged: false },
            },
            opts
        )
    )

describe('EtlScriptEditors', () => {
    let wrapper
    const editorComponents = [
        {
            target: 'select-script',
            dataField: 'select',
            labelField: 'retrieveDataFromSrc',
        },
        {
            target: 'create-script',
            dataField: 'create',
            labelField: 'createObjInDest',
        },
        {
            target: 'insert-script',
            dataField: 'insert',
            labelField: 'insertDataInDest',
        },
    ]
    editorComponents.forEach(({ target, dataField, labelField }) => {
        it(`Should pass accurate data to ${target} etl-editor`, () => {
            wrapper = mountFactory()
            const { value, label, skipRegEditorCompleters } = wrapper.find(
                `[data-test="${target}"]`
            ).vm.$props
            expect(value).to.equal(wrapper.vm.stagingRow[dataField])
            expect(label).to.equal(wrapper.vm.$mxs_t(labelField))
            expect(skipRegEditorCompleters).to.be[dataField !== 'select']
        })
    })

    const isInErrStateTests = [false, true]
    isInErrStateTests.forEach(v => {
        it(`Should${v ? '' : ' not'} render an error message when isInErrState is ${v}`, () => {
            wrapper = mountFactory({ computed: { isInErrState: () => v } })
            const ele = wrapper.find('[data-test="script-err-msg"]')
            if (v) expect(ele.text()).to.equal(wrapper.vm.$mxs_t('errors.scriptCanNotBeEmpty'))
            else expect(ele.exists()).to.be.false
        })
    })

    const hasChangedTests = [false, true]
    hasChangedTests.forEach(v => {
        it(`Should${v ? '' : ' not'} render discard-btn when hasChanged is ${v}`, () => {
            wrapper = mountFactory({ propsData: { hasChanged: v } })
            const ele = wrapper.find('[data-test="discard-btn"]')
            expect(ele.exists()).to.be[v]
        })
    })

    const testCases = [
        {
            name: 'select is undefined',
            data: { create: 'create script', insert: 'insert script' },
            expected: true,
        },
        {
            name: 'create is an empty string',
            data: { select: 'select script', create: '', insert: 'insert script' },
            expected: true,
        },
        {
            name: 'insert is null',
            data: { select: 'select script', create: 'create script', insert: null },
            expected: true,
        },
        {
            name: 'all properties are defined and not empty',
            data: { select: 'select script', create: 'create script', insert: 'insert script' },
            expected: false,
        },
    ]

    testCases.forEach(testCase => {
        it(`Should return ${testCase.expected} when ${testCase.name}`, () => {
            wrapper = mountFactory({ computed: { stagingRow: () => testCase.data } })
            expect(wrapper.vm.isInErrState).to.be[testCase.expected]
        })
    })

    it(`Should return accurate value for stagingRow`, () => {
        wrapper = mountFactory()
        expect(wrapper.vm.stagingRow).to.eql(valueStub)
    })

    it(`Should emit input event when stagingRow is changed`, () => {
        wrapper = mountFactory()
        const newValue = { ...valueStub, select: 'new value' }
        wrapper.vm.stagingRow = newValue
        expect(wrapper.emitted('input')[0][0]).to.eql(newValue)
    })
})
