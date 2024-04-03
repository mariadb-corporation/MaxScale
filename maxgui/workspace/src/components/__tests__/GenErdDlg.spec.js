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
import GenErdDlg from '@wsComps/GenErdDlg.vue'
import QueryConn from '@wsModels/QueryConn'
import queryHelper from '@wsSrc/store/queryHelper'
import { lodash } from '@share/utils/helpers'

const genErdDlgDataStub = {
    is_opened: false,
    preselected_schemas: [],
    connection: null,
    gen_in_new_ws: false, // generate erd in a new worksheet
}

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: GenErdDlg,
                computed: {
                    gen_erd_dlg: () => genErdDlgDataStub,
                },
            },
            opts
        )
    )

describe('GenErdDlg', () => {
    let wrapper
    afterEach(() => sinon.restore())
    describe(`Child component's data communication tests`, () => {
        beforeEach(() => {
            wrapper = mountFactory()
        })

        it('Should pass accurate data to mxs-dlg', () => {
            const {
                value,
                title,
                saveText,
                allowEnterToSubmit,
                onSave,
                hasSavingErr,
            } = wrapper.findComponent({
                name: 'mxs-dlg',
            }).vm.$props
            expect(value).to.equals(wrapper.vm.isOpened)
            expect(title).to.equals(wrapper.vm.$mxs_t('selectObjsToVisualize'))
            expect(saveText).to.equals('visualize')
            expect(allowEnterToSubmit).to.be.false
            expect(hasSavingErr).to.equals(wrapper.vm.hasSavingErr)
            expect(onSave).to.be.eql(wrapper.vm.visualize)
        })

        it('Should pass accurate data to name input', () => {
            const {
                $attrs: { value, required },
                $props: { label },
            } = wrapper.findComponent({
                name: 'mxs-label-field',
            }).vm
            expect(value).to.equals(wrapper.vm.$data.name)
            expect(label).to.equals(wrapper.vm.$mxs_t('name'))
            expect(required).to.be.true
        })

        it('Should pass accurate data to selectable-schema-table-tree', () => {
            const {
                connId,
                preselectedSchemas,
                triggerDataFetch,
                excludeNonFkSupportedTbl,
            } = wrapper.findComponent({
                name: 'selectable-schema-table-tree',
            }).vm.$props
            expect(connId).to.equals(wrapper.vm.connId)
            expect(preselectedSchemas).to.eql(wrapper.vm.preselectedSchemas)
            expect(triggerDataFetch).to.equal(wrapper.vm.isOpened)
            expect(excludeNonFkSupportedTbl).to.be.true
        })

        it(`Should render info text`, () => {
            expect(wrapper.find('[data-test="erd-support-table-info"]').exists()).to.be.true
        })

        it(`Should render error text when there is an error when generate ERD`, async () => {
            expect(wrapper.find('[data-test="err-msg"]').exists()).to.be.false
            await wrapper.setData({ errMsg: 'Some error' })
            expect(wrapper.find('[data-test="err-msg"]').exists()).to.be.true
        })
    })
    describe(`Computed properties and method tests`, () => {
        it(`Should return accurate value for isOpened`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.isOpened).to.equal(wrapper.vm.gen_erd_dlg.is_opened)
        })

        it(`Should call SET_GEN_ERD_DLG when isOpened is changed`, () => {
            wrapper = mountFactory()
            const spy = sinon.spy(wrapper.vm, 'SET_GEN_ERD_DLG')
            wrapper.vm.isOpened = true
            spy.should.have.been.calledOnceWithExactly({ ...genErdDlgDataStub, is_opened: true })
        })

        it('hasSavingErr should return true when there is error in visualizing ERD', () => {
            wrapper = mountFactory({ data: () => ({ errMsg: 'An error message' }) })
            expect(wrapper.vm.hasSavingErr).to.be.true
        })

        it('hasSavingErr should return true when selectedTargets is empty', () => {
            wrapper = mountFactory({ data: () => ({ selectedTargets: [] }) })
            expect(wrapper.vm.hasSavingErr).to.be.true
        })
        const connStub = { id: 'conn-id', meta: { name: 'server_0' } }
        const configStub = {}
        const responseErrStub = { response: { data: { errors: [{ detail: 'query error' }] } } }

        it('handleCloneConn should set error message to errMsg if it fails to clone', async () => {
            const errsStub = ['query error']
            wrapper = mountFactory({
                mocks: {
                    $helpers: {
                        tryAsync: async () => [responseErrStub, null],
                        getErrorsArr: () => errsStub,
                    },
                },
            })
            await wrapper.vm.handleCloneConn({ conn: connStub, config: configStub })
            expect(wrapper.vm.errMsg).to.equal('query error')
        })

        it('handleCloneConn should return an object if it clones successfully', async () => {
            const resStub = { data: { data: { id: 'conn-id-1' } } }
            wrapper = mountFactory({
                mocks: { $helpers: { tryAsync: async () => [null, resStub] } },
            })
            const result = await wrapper.vm.handleCloneConn({ conn: connStub, config: configStub })
            expect(result).to.eql(resStub.data.data)
        })

        it('Should call methods in handleQueryData correctly', async () => {
            wrapper = mountFactory()
            // stub methods
            const queryConnStub = sinon.stub(QueryConn, 'dispatch').resolves()
            const queryDdlEditorSuppDataStub = sinon
                .stub(wrapper.vm, 'queryDdlEditorSuppData')
                .resolves()
            const queryAndParseTblDDLStub = sinon
                .stub(queryHelper, 'queryAndParseTblDDL')
                .resolves([responseErrStub, []])

            await wrapper.vm.handleQueryData({ conn: connStub, config: configStub })

            queryConnStub.calledOnceWithExactly('enableSqlQuoteShowCreate', {
                connId: connStub.id,
                config: configStub,
            })
            queryDdlEditorSuppDataStub.calledOnceWithExactly('queryDdlEditorSuppDataStub', {
                connId: connStub.id,
                config: configStub,
            })
            queryAndParseTblDDLStub.calledOnceWithExactly('queryDdlEditorSuppDataStub', {
                connId: connStub.id,
                targets: wrapper.vm.$data.selectedTargets,
                config: configStub,
                charsetCollationMap: wrapper.vm.charset_collation_map,
            })
        })

        it('handleQueryData should return object data as expected', async () => {
            wrapper = mountFactory()
            const parsedTablesStub = []
            sinon.stub(QueryConn, 'dispatch').resolves()
            sinon.stub(wrapper.vm, 'queryDdlEditorSuppData').resolves()
            sinon.stub(queryHelper, 'queryAndParseTblDDL').resolves([null, parsedTablesStub])
            const data = await wrapper.vm.handleQueryData({ conn: connStub, config: configStub })
            expect(data).to.have.all.keys('erdTaskData', 'erdTaskTmpData')
            expect(data.erdTaskData).to.have.all.keys('nodeMap', 'is_laid_out')
            expect(data.erdTaskTmpData).to.have.all.keys(
                'graph_height_pct',
                'active_entity_id',
                'key',
                'nodes_history',
                'active_history_idx'
            )
        })

        const erdTaskDataStub = { nodeMap: {}, is_laid_out: false }
        const erdTaskTmpDataStub = {
            graph_height_pct: 100,
            active_entity_id: '',
            key: 'key_id',
            nodes_history: [],
            active_history_idx: 0,
        }
        it('Should call handleCloneConn and visualizeInNewWs when genInNewWs is true', async () => {
            wrapper = mountFactory({ computed: { conn: () => connStub, genInNewWs: () => true } })
            const handleCloneConnStub = sinon.stub(wrapper.vm, 'handleCloneConn').resolves(connStub)
            const visualizeInNewWsStub = sinon.stub(wrapper.vm, 'visualizeInNewWs')
            sinon
                .stub(wrapper.vm, 'handleQueryData')
                .resolves({ erdTaskData: erdTaskDataStub, erdTaskTmpData: erdTaskTmpDataStub })

            await wrapper.vm.visualize()
            handleCloneConnStub.should.have.been.calledOnceWithExactly({
                conn: connStub,
                config: configStub,
            })
            visualizeInNewWsStub.should.have.been.calledOnceWithExactly({
                conn: connStub,
                connMeta: connStub.meta,
                erdTaskData: erdTaskDataStub,
                erdTaskTmpData: erdTaskTmpDataStub,
            })
        })

        it('Should call visualizeInCurrentWs when genInNewWs is false', async () => {
            wrapper = mountFactory({ computed: { conn: () => connStub, genInNewWs: () => false } })
            const visualizeInCurrentWsStub = sinon.stub(wrapper.vm, 'visualizeInCurrentWs')
            sinon
                .stub(wrapper.vm, 'handleQueryData')
                .resolves({ erdTaskData: erdTaskDataStub, erdTaskTmpData: erdTaskTmpDataStub })

            await wrapper.vm.visualize()
            visualizeInCurrentWsStub.should.have.been.calledOnceWithExactly({
                erdTaskData: erdTaskDataStub,
                erdTaskTmpData: erdTaskTmpDataStub,
            })
        })
    })
})
