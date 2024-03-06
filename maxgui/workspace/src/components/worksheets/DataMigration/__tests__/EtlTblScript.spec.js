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
import EtlTblScript from '@wkeComps/DataMigration/EtlTblScript'
import { lodash } from '@share/utils/helpers'
import { task } from '@wkeComps/DataMigration/__tests__/stubData'

const dataStub = [
    {
        create: 'create script',
        insert: 'insert script',
        select: 'select script',
        schema: 'Workspace',
        table: 'AnalyzeEditor',
    },
    {
        create: 'create script',
        insert: 'insert script',
        select: 'select script',
        schema: 'Workspace',
        table: 'DdlEditor',
    },
]

const headersStub = [
    { text: 'SCHEMA', value: 'schema', cellClass: 'truncate-cell', width: '50%' },
    { text: 'TABLE', value: 'table', cellClass: 'truncate-cell', width: '50%' },
]
const mountFactory = opts =>
    mount(
        lodash.merge(
            { shallow: true, component: EtlTblScript, propsData: { data: dataStub, task } },
            opts
        )
    )

describe('EtlTblScript', () => {
    let wrapper

    describe("Child component's data communication tests", () => {
        it(`Should pass accurate data to mxs-data-table`, () => {
            wrapper = mountFactory()
            const {
                value,
                items,
                ['fixed-header']: fixedHeader,
                ['hide-default-footer']: hideDefaultFooter,
                ['items-per-page']: itemsPerPage,
                height,
            } = wrapper.findComponent({ name: 'mxs-data-table' }).vm.$attrs
            expect(value).to.eql(wrapper.vm.$data.selectedItems)
            expect(items).to.eql(wrapper.vm.tableRows)
            expect(fixedHeader).to.not.be.undefined
            expect(hideDefaultFooter).to.not.be.undefined
            expect(itemsPerPage).to.equal(-1)
            expect(height).to.equal(wrapper.vm.$data.tableMaxHeight)
        })

        it(`Should pass $attrs to mxs-data-table`, () => {
            wrapper = mountFactory({ attrs: { headers: headersStub } })
            const { headers } = wrapper.findComponent({ name: 'mxs-data-table' }).vm.$attrs
            expect(headers).to.eql(headersStub)
        })

        it(`Should render mxs-data-table slots`, () => {
            wrapper = mountFactory({
                slots: { ['item.schema']: '<div data-test="item-schema-slot"/>' },
            })
            expect(wrapper.find('[data-test="item-schema-slot"]').exists()).to.be.true
        })

        it(`Should not render etl-script-editors when shouldShowScriptEditors is false`, () => {
            wrapper = mountFactory({ computed: { shouldShowScriptEditors: () => false } })
            expect(wrapper.findComponent({ name: 'etl-script-editors' }).exists()).to.be.false
        })

        it(`Should pass accurate data to etl-script-editors`, () => {
            wrapper = mountFactory({ computed: { shouldShowScriptEditors: () => true } })
            const { value, hasChanged } = wrapper.findComponent({
                name: 'etl-script-editors',
            }).vm.$props
            expect(value).to.eql(wrapper.vm.activeRow)
            expect(hasChanged).to.equal(wrapper.vm.hasChanged)
        })
    })
    describe('Computed tests', () => {
        it(`defDataMap should generate UUID and return a map`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.defDataMap).to.be.an('object')
            Object.values(wrapper.vm.defDataMap).forEach(item =>
                expect(item).to.have.all.keys('create', 'insert', 'select', 'schema', 'table', 'id')
            )
        })

        const booleanProperties = [
            'hasChanged',
            'isRunning',
            'hasScriptFields',
            'shouldShowScriptEditors',
        ]
        booleanProperties.forEach(property =>
            it(`${property} should return a boolean`, () => {
                wrapper = mountFactory()
                expect(wrapper.vm[property]).to.be.a('boolean')
            })
        )

        it(`activeRow should return the first object in selectedItems`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.activeRow).to.eql(wrapper.vm.$data.selectedItems[0])
        })

        it(`Should update selectedItems when activeRow is changed`, () => {
            wrapper = mountFactory()
            const newData = { ...wrapper.vm.activeRow, create: '' }
            wrapper.vm.activeRow = newData
            expect(wrapper.vm.$data.selectedItems).to.eql([newData])
        })

        it(`stagingData should return object without 'id' field`, () => {
            wrapper = mountFactory()
            wrapper.vm.stagingData.forEach(obj => expect(obj).to.not.haveOwnProperty('id'))
        })

        const hasScriptFieldsTestCases = [
            {
                name: 'select is undefined',
                data: { create: 'create script', insert: 'insert script' },
                expected: false,
            },
            {
                name: 'create is an empty string',
                data: { select: 'select script', create: '', insert: 'insert script' },
                expected: true,
            },
            {
                name: 'insert is undefined',
                data: { select: 'select script', create: 'create script', insert: undefined },
                expected: false,
            },
            {
                name: 'all properties are defined',
                data: { select: 'select script', create: 'create script', insert: 'insert script' },
                expected: true,
            },
        ]

        hasScriptFieldsTestCases.forEach(testCase => {
            it(`Should return ${testCase.expected} when ${testCase.name}`, () => {
                wrapper = mountFactory({ computed: { activeRow: () => testCase.data } })
                expect(wrapper.vm.hasScriptFields).to.be[testCase.expected]
            })
        })
    })
    describe('Watcher tests', () => {
        afterEach(() => sinon.restore())

        it(`Should immediately emit get-staging-data event`, () => {
            wrapper = mountFactory()
            expect(wrapper.emitted('get-staging-data')).to.be.an('array').that.is.not.empty
        })

        it(`defDataMap handler should immediately set tableRows`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.$data.tableRows).to.eql(Object.values(wrapper.vm.defDataMap))
        })

        it(`defDataMap handler should immediately select the first row as active`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.$data.selectedItems).to.eql([wrapper.vm.tableRows[0]])
        })

        it(`defDataMap handler should immediately select the first row having the error field
        as active`, () => {
            const firstErrObjStub = { ...dataStub[0], error: 'Error' }
            wrapper = mountFactory({ computed: { firstErrObj: () => firstErrObjStub } })
            expect(wrapper.vm.$data.selectedItems).to.eql([firstErrObjStub])
        })

        it(`defDataMap handler should call setTblMaxHeight method`, () => {
            const spy = sinon.spy(EtlTblScript.methods, 'setTblMaxHeight')
            wrapper = mountFactory()
            spy.calledOnce
        })

        it(`Should immediately emit get-activeRow event`, () => {
            wrapper = mountFactory()
            expect(wrapper.emitted('get-activeRow')[0][0]).to.eql(wrapper.vm.activeRow)
        })
    })
})
