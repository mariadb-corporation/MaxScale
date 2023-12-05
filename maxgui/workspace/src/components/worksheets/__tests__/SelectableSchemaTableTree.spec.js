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
import SelectableSchemaTableTree from '@wkeComps/SelectableSchemaTableTree.vue'
import { lodash } from '@share/utils/helpers'
import queryHelper from '@wsSrc/store/queryHelper'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: SelectableSchemaTableTree,
                propsData: {
                    connId: '923a1239-d8fb-4991-b8f7-3ca3201c12f4',
                    preselectedSchemas: [],
                    triggerDataFetch: false,
                    excludeNonFkSupportedTbl: false,
                },
            },
            opts
        )
    )

const stubSchemaNode = { type: 'SCHEMA', name: 'information_schema' }
const stubTblNode = {
    parentNameData: { SCHEMA: 'test' },
    type: 'TABLE',
    name: 'QueryConn',
}
const stubTargets = [{ tbl: 't1', schema: 'test' }]

describe('SelectableSchemaTableTree', () => {
    let wrapper

    it(`Should pass accurate data to mxs-treeview`, () => {
        wrapper = mountFactory()
        const { value, items, openOnClick, selectable, returnObject } = wrapper.findComponent({
            name: 'v-treeview',
        }).vm.$props
        expect(value).to.equal(wrapper.vm.$data.selectedObjs)
        expect(items).to.eql(wrapper.vm.$data.items)
        expect(openOnClick).to.be.true
        expect(selectable).to.be.true
        expect(returnObject).to.be.true
    })

    it(`Should render input message text`, () => {
        wrapper = mountFactory({
            data: () => ({ inputMsgObj: { type: 'warning', text: 'warning text' } }),
        })
        expect(wrapper.find('[data-test="input-msg"]').html()).to.contain('warning text')
    })

    it(`Should render query error message text`, () => {
        wrapper = mountFactory({ data: () => ({ queryErrMsg: 'query error text' }) })
        expect(wrapper.find('[data-test="query-err-msg"]').html()).to.contain('query error text')
    })

    describe('computed tests', () => {
        it(`categorizeObjs should return accurate properties`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.categorizeObjs).to.have.all.keys('emptySchemas', 'targets')
        })

        it(`categorizeObjs should includes empty schemas in emptySchemas property`, () => {
            wrapper = mountFactory({ data: () => ({ selectedObjs: [stubSchemaNode] }) })
            expect(wrapper.vm.categorizeObjs.emptySchemas[0]).to.equal(stubSchemaNode.name)
        })

        it(`categorizeObjs should include accurate target objects`, () => {
            wrapper = mountFactory({ data: () => ({ selectedObjs: [stubTblNode] }) })
            expect(wrapper.vm.categorizeObjs.targets[0]).to.eql({
                tbl: stubTblNode.name,
                schema: stubTblNode.parentNameData.SCHEMA,
            })
        })
    })

    describe('watcher tests', () => {
        afterEach(() => sinon.restore())

        const triggerDataFetchTestCases = [true, false]
        triggerDataFetchTestCases.forEach(testCase => {
            it(`When triggerDataFetch is ${testCase}, expected methods
            should ${testCase ? '' : 'not '}be called`, async () => {
                const methods = ['fetchSchemas', 'handlePreselectedSchemas']
                wrapper = mountFactory({
                    data: () => ({ items: [{ type: 'SCHEMA', name: 'company', children: [] }] }),
                })
                const stubs = methods.map(method => sinon.stub(wrapper.vm, method).resolves())
                await wrapper.setProps({ triggerDataFetch: testCase })
                await wrapper.vm.$nextTick()
                if (testCase) expect(wrapper.vm.items).to.eql([])
                stubs.forEach(stub => {
                    if (testCase) stub.should.have.been.calledOnce
                    else stub.should.not.have.been.called
                })
            })
        })

        it(`selectedObjs should set error message when targets is empty`, async () => {
            wrapper = mountFactory({ computed: { targets: () => [] } })
            await wrapper.setData({ selectedObjs: [stubSchemaNode] })
            expect(wrapper.vm.$data.inputMsgObj).to.eql({
                type: 'error',
                text: wrapper.vm.$mxs_t('errors.emptyVisualizeSchema'),
            })
        })

        it(`selectedObjs should set warning message when targets is not empty but there
        are schemas with empty tables`, async () => {
            wrapper = mountFactory({ computed: { targets: () => stubTargets } })
            await wrapper.setData({ selectedObjs: [stubSchemaNode, stubTblNode] })
            expect(wrapper.vm.$data.inputMsgObj).to.eql({
                type: 'warning',
                text: wrapper.vm.$mxs_t('warnings.ignoredVisualizeSchemas'),
            })
        })

        it(`selectedObjs should set null to inputMsgObj when selectedObjs is empty`, async () => {
            wrapper = mountFactory({ data: () => ({ selectedObjs: [stubSchemaNode] }) })
            await wrapper.setData({ selectedObjs: [] })
            expect(wrapper.vm.$data.inputMsgObj).to.be.null
        })

        it(`Should emit selected-targets event`, () => {
            wrapper = mountFactory({ computed: { targets: () => stubTargets } })
            expect(wrapper.emitted('selected-targets')[0][0]).to.be.eql(stubTargets)
        })
    })

    describe('method tests', () => {
        const responseErrStub = { response: { data: { errors: [{ detail: 'query error' }] } } }
        it(`Should set queryErrMsg when an error occurs during fetchSchemas`, async () => {
            wrapper = mountFactory({
                mocks: { $helpers: { to: async () => [responseErrStub, null] } },
            })
            await wrapper.vm.fetchSchemas()
            expect(wrapper.vm.$data.queryErrMsg).to.equal(
                wrapper.vm.$mxs_t('errors.retrieveSchemaObj')
            )
        })

        it(`fetchSchemas should set queryErrMsg when receive query error response`, async () => {
            const queryResStub = { data: { data: { attributes: { results: [{ errno: 1410 }] } } } }
            wrapper = mountFactory({
                mocks: {
                    $helpers: {
                        to: async () => [null, queryResStub],
                        queryResErrToStr: () => 'errno: 1410',
                    },
                },
            })
            await wrapper.vm.fetchSchemas()
            expect(wrapper.vm.$data.queryErrMsg).to.equal('errno: 1410')
        })

        it('handlePreselectedSchemas should be handled as expected', async () => {
            let mockNode = { qualified_name: 'schema1', children: [] },
                loadTablesCallCount = 0
            wrapper = mountFactory({
                methods: {
                    loadTables: () => {
                        loadTablesCallCount++
                        mockNode.children = [{ name: 'table1' }]
                    },
                },
            })
            await wrapper.setData({ items: [mockNode] })
            await wrapper.setProps({ preselectedSchemas: ['schema1'] })
            await wrapper.vm.handlePreselectedSchemas()
            expect(loadTablesCallCount).to.equal(1)
            expect(wrapper.vm.$data.selectedObjs).to.eql(mockNode.children)
        })

        it('loadTables should be handled as expected', async () => {
            let mockNode = {
                id: 'node_id',
                qualified_name: 'schema1',
                parentNameData: { SCHEMA: 'schema1' },
                level: 0,
                children: [],
            }
            wrapper = mountFactory()
            const getChildNodeDataMock = sinon
                .stub(queryHelper, 'getChildNodes')
                .resolves({ name: 'table1' })

            await wrapper.vm.loadTables(mockNode)
            getChildNodeDataMock.should.have.been.calledOnce
        })
    })
})
