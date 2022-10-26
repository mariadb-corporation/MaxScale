/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import SidebarCtr from '../SidebarCtr.vue'

const mountFactory = opts =>
    mount({
        shallow: true,
        component: SidebarCtr,
        stubs: {
            'sql-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

function mockShowingDbListTree() {
    return {
        getLoadingDbTree: () => false,
        active_sql_conn: () => ({ id: '1', name: 'server_0', type: 'servers' }),
    }
}

describe('sidebar-ctr', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        const fnEvtMap = {
            handleGetNodeData: 'get-node-data',
            handleLoadChildren: 'load-children',
            useDb: 'use-db',
            onAlterTable: 'alter-tbl',
            onDropAction: 'drop-action',
            onTruncateTbl: 'truncate-tbl',
        }
        const getNodeDataParam = { SQL_QUERY_MODE: 'PRVW_DATA', schemaId: 'test.t1' }
        const dummyNode = { key: 'node_key_20', type: 'Table', name: 't1', id: 'test.t1' }
        const useDbParam = 'test'
        const onDropActionParam = { id: dummyNode.id, type: dummyNode.type }
        const onTruncateTblParam = 't1'
        Object.keys(fnEvtMap).forEach(key => {
            it(`Should call ${key} if ${fnEvtMap[key]} is emitted from schema-tree-ctr`, () => {
                const spyFn = sinon.spy(SidebarCtr.methods, key)
                wrapper = mountFactory({
                    computed: { ...mockShowingDbListTree() },
                })
                const dbListTree = wrapper.findComponent({ name: 'schema-tree-ctr' })
                let param
                switch (key) {
                    case 'handleGetNodeData':
                        param = getNodeDataParam
                        break
                    case 'handleLoadChildren':
                    case 'onAlterTable':
                        param = dummyNode
                        break
                    case 'useDb':
                        param = useDbParam
                        break
                    case 'onDropAction':
                        param = onDropActionParam
                        break
                    case 'onTruncateTbl':
                        param = onTruncateTblParam
                        break
                }
                dbListTree.vm.$emit(fnEvtMap[key], param)
                spyFn.should.have.been.calledOnceWith(param)
                spyFn.restore()
            })
        })
    })

    describe(`computed properties tests`, () => {
        let wrapper
        it(`Should return accurate value for hasConn`, () => {
            // have no connection
            wrapper = mountFactory()
            expect(wrapper.vm.hasConn).to.be.false
            // Have valid connection
            wrapper = mountFactory({
                computed: { ...mockShowingDbListTree() },
            })
            expect(wrapper.vm.hasConn).to.be.true
        })
        it(`Should return accurate value for reloadDisabled`, async () => {
            // has connection
            wrapper = mountFactory({
                computed: {
                    hasConn: () => true,
                },
            })
            expect(wrapper.vm.reloadDisabled).to.be.false
            // have no connection and still loading for data
            await wrapper.setProps({ hasConn: false, isLoading: true })
            wrapper = mountFactory({
                computed: {
                    hasConn: () => false,
                    getLoadingDbTree: () => true,
                },
            })
            expect(wrapper.vm.reloadDisabled).to.be.true
        })
    })

    describe(`Methods tests`, () => {
        let wrapper
        it(`Should process handleGetNodeData method as expected`, () => {
            let clearDataPreviewCallCount = 0
            let queryModeParam, fetchPrvwParams
            const active_session_id = 'SESSION_123_45'
            wrapper = mountFactory({
                methods: {
                    clearDataPreview: () => clearDataPreviewCallCount++,
                    SET_CURR_QUERY_MODE: mode => (queryModeParam = mode),
                    fetchPrvw: params => (fetchPrvwParams = params),
                },
                computed: { getActiveSessionId: () => active_session_id },
            })
            const mockParam = { SQL_QUERY_MODE: 'PRVW_DATA', schemaId: 'test.t1' }
            wrapper.vm.handleGetNodeData(mockParam)
            expect(clearDataPreviewCallCount).to.be.equals(1)
            expect(queryModeParam).to.be.eql({
                payload: mockParam.SQL_QUERY_MODE,
                id: active_session_id,
            })
            expect(fetchPrvwParams).to.be.deep.equals({
                tblId: mockParam.schemaId,
                prvwMode: mockParam.SQL_QUERY_MODE,
            })
        })
        it(`Should call loadChildNodes when handleLoadChildren is called`, () => {
            let loadChildNodesParam
            wrapper = mountFactory({
                methods: {
                    loadChildNodes: param => (loadChildNodesParam = param),
                },
            })
            const mockNode = {
                key: 'node_key_20',
                type: 'Tables',
                name: 'Tables',
                id: 'test.Tables',
                qualified_name: 'test.Tables',
            }
            wrapper.vm.handleLoadChildren(mockNode)
            expect(loadChildNodesParam).to.be.deep.equals(mockNode)
        })
        it(`Should process onAlterTable method as expected`, async () => {
            let queryTblCreationInfoParam
            wrapper = mountFactory({
                computed: {
                    engines: () => [],
                    charset_collation_map: () => ({}),
                    def_db_charset_map: () => ({}),
                },
                methods: {
                    handleAddNewSession: () => null,
                    queryAlterTblSuppData: () => null,
                    queryTblCreationInfo: param => (queryTblCreationInfoParam = param),
                },
            })
            const mockNode = {
                key: 'node_key_20',
                type: 'Table',
                name: 't1',
                id: 'test.Tables.t1',
                qualified_name: 'test.t1',
            }
            const fnsToBeSpied = [
                'handleAddNewSession',
                'queryAlterTblSuppData',
                'queryTblCreationInfo',
            ]
            fnsToBeSpied.forEach(fn => {
                sinon.spy(wrapper.vm, fn)
            })

            await wrapper.vm.onAlterTable(mockNode) // trigger the method

            expect(queryTblCreationInfoParam).to.be.deep.equals(mockNode)
            fnsToBeSpied.forEach(fn => {
                wrapper.vm[fn].should.have.been.calledOnce
                wrapper.vm[fn].restore()
            })
        })
        it(`Should process onDropAction method as expected`, () => {
            wrapper = mountFactory()
            const spy = sinon.spy(wrapper.vm, 'handleOpenExecSqlDlg')
            const mockNode = { type: 'Table', id: 'test.Tables.t1', qualified_name: 'test.t1' }
            wrapper.vm.onDropAction(mockNode) // trigger the method
            spy.should.have.been.calledOnceWith('DROP TABLE `test`.`t1`;')
            expect(wrapper.vm.actionName).to.be.equals('DROP TABLE `test`.`t1`')
            spy.restore()
        })
        it(`Should process onTruncateTbl method as expected`, () => {
            wrapper = mountFactory()
            const spy = sinon.spy(wrapper.vm, 'handleOpenExecSqlDlg')
            wrapper.vm.onTruncateTbl('test.t1') // trigger the method
            spy.should.have.been.calledOnceWith('truncate `test`.`t1`;')
            expect(wrapper.vm.actionName).to.be.equals('truncate `test`.`t1`')
            spy.restore()
        })
        it(`Should call exeStmtAction method when confirmExeStatements is called`, () => {
            const mockSql = 'truncate `test`.`t1`;'
            const mockActionName = 'truncate `test`.`t1`'
            wrapper = mountFactory({
                propsData: { execSqlDlg: { sql: mockSql } },
                data: () => ({ actionName: mockActionName }),
            })
            sinon.spy(wrapper.vm, 'exeStmtAction')
            wrapper.vm.confirmExeStatements() // trigger the method
            wrapper.vm.exeStmtAction.should.have.been.calledOnceWith({
                sql: mockSql,
                action: mockActionName,
            })
            wrapper.vm.exeStmtAction.restore()
        })
        it(`Should call PATCH_EXE_STMT_RESULT_MAP mutation when
          clearExeStatementsResult is called`, () => {
            const mockActive_wke_id = 'wke_abcd'
            wrapper = mountFactory({
                computed: { active_wke_id: () => mockActive_wke_id },
                methods: { PATCH_EXE_STMT_RESULT_MAP: () => null },
            })
            sinon.spy(wrapper.vm, 'PATCH_EXE_STMT_RESULT_MAP')
            wrapper.vm.clearExeStatementsResult() // trigger the method
            wrapper.vm.PATCH_EXE_STMT_RESULT_MAP.should.have.been.calledWith({
                id: mockActive_wke_id,
            })
            wrapper.vm.PATCH_EXE_STMT_RESULT_MAP.restore()
        })
    })

    describe(`Button tests`, () => {
        it(`Should disable reload-schemas button`, () => {
            wrapper = mountFactory({
                shallow: false,
                computed: { reloadDisabled: () => true },
            })
            expect(wrapper.find('.reload-schemas').attributes().disabled).to.be.equals('disabled')
        })
        it(`Should disable filter-objects input`, () => {
            wrapper = mountFactory({
                shallow: false,
                computed: { reloadDisabled: () => true },
            })
            expect(
                wrapper
                    .find('.filter-objects')
                    .find('input')
                    .attributes().disabled
            ).to.be.equals('disabled')
        })

        const btnHandlerMap = {
            'reload-schemas': 'fetchSchemas',
            'toggle-sidebar': 'SET_IS_SIDEBAR_COLLAPSED',
        }
        Object.keys(btnHandlerMap).forEach(btn => {
            it(`Should call ${btnHandlerMap[btn]} when ${btn} button is clicked`, async () => {
                let callCount = 0
                wrapper = mountFactory({
                    shallow: false,
                    computed: { reloadDisabled: () => false, is_sidebar_collapsed: () => false },
                    methods: {
                        [btnHandlerMap[btn]]: () => callCount++,
                    },
                })
                await wrapper.find(`.${btn}`).trigger('click')
                console.log('callCount', callCount)
                expect(callCount).to.be.equals(1)
            })
        })
    })
})
