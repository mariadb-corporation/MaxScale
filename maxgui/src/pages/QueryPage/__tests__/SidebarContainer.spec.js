/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import SidebarContainer from '@/pages/QueryPage/SidebarContainer'

const mountFactory = opts =>
    mount({
        shallow: true,
        component: SidebarContainer,
        stubs: {
            'query-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

function mockShowingDbListTree() {
    return {
        getLoadingDbTree: () => false,
        curr_cnct_resource: () => ({ id: '1', name: 'server_0', type: 'servers' }),
    }
}
function mockLoadingDbListTree() {
    return {
        getLoadingDbTree: () => true,
    }
}
const dummy_exe_stmt_result = {
    stmt_err_msg_obj: {
        errno: 1064,
        message: 'dummy message',
        sqlstate: '42000',
    },
}
describe(`SidebarContainer - child component's data communication tests`, () => {
    let wrapper
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
        it(`Should call ${key} if ${fnEvtMap[key]} event is emitted from db-list-tree`, () => {
            const spyFn = sinon.spy(SidebarContainer.methods, key)
            wrapper = mountFactory({
                computed: { ...mockShowingDbListTree() },
            })
            const dbListTree = wrapper.findComponent({ name: 'db-list-tree' })
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

    it(`Should pass accurate data to execute-sql-dialog via props`, () => {
        wrapper = mountFactory({ isExeStatementsFailed: () => false })
        const {
            value,
            title,
            smallInfo,
            hasSavingErr,
            errMsgObj,
            sqlTobeExecuted,
            onSave,
        } = wrapper.findComponent({
            name: 'execute-sql-dialog',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.$data.isExeDlgOpened)
        expect(title).to.be.equals(
            wrapper.vm.$tc('confirmations.exeStatements', wrapper.vm.stmtI18nPluralization)
        )
        expect(smallInfo).to.be.equals(
            wrapper.vm.$tc('info.exeStatementsInfo', wrapper.vm.stmtI18nPluralization)
        )
        expect(hasSavingErr).to.be.equals(wrapper.vm.isExeStatementsFailed)
        expect(errMsgObj).to.be.deep.equals(wrapper.vm.stmtErrMsgObj)
        expect(sqlTobeExecuted).to.be.equals(wrapper.vm.$data.sql)
        expect(onSave).to.be.equals(wrapper.vm.confirmExeStatements)
    })
    const evts = ['after-close', 'after-cancel']
    evts.forEach(evt => {
        it(`Should call clearExeStatementsResult when ${evt} is emitted
        from execute-sql-dialog`, () => {
            const clearExeStatementsResultSpy = sinon.spy(
                SidebarContainer.methods,
                'clearExeStatementsResult'
            )
            wrapper = mountFactory()
            const dlg = wrapper.findComponent({ name: 'execute-sql-dialog' })
            dlg.vm.$emit(evt)
            clearExeStatementsResultSpy.should.have.been.calledOnce
            clearExeStatementsResultSpy.restore()
        })
    })
})
describe(`SidebarContainer - computed properties tests`, () => {
    let wrapper
    it(`Should return accurate value for isConnecting`, () => {
        // still loading data or have no connection
        wrapper = mountFactory({ computed: { ...mockLoadingDbListTree() } })
        expect(wrapper.vm.isConnecting).to.be.true
        // Have valid connection and data
        wrapper = mountFactory({
            computed: { ...mockShowingDbListTree() },
        })
        expect(wrapper.vm.isConnecting).to.be.false
    })
    it(`Should return accurate value for stmtI18nPluralization`, () => {
        // when sql is empty or has only a statement
        wrapper = mountFactory()
        expect(wrapper.vm.stmtI18nPluralization).to.be.equals(1)
        // when sql has more than one statement
        wrapper = mountFactory({ data: () => ({ sql: 'SELET 1; SELLET 2; SELECT 3;' }) })
        expect(wrapper.vm.stmtI18nPluralization).to.be.equals(2)
    })
    it(`Should return accurate value for isExeStatementsFailed`, () => {
        // When there is no statement has been executed yet
        wrapper = mountFactory()
        expect(wrapper.vm.isExeStatementsFailed).to.be.false
        // When there is an error message
        wrapper = mountFactory({
            computed: { getExeStmtResultMap: () => dummy_exe_stmt_result },
        })
        expect(wrapper.vm.isExeStatementsFailed).to.be.true
    })
    it(`Should return accurate value for stmtErrMsgObj`, () => {
        // When there is no statement has been executed yet
        wrapper = mountFactory()
        expect(wrapper.vm.stmtErrMsgObj).to.be.an('object').and.be.empty
        // When there is an error message
        wrapper = mountFactory({
            computed: { getExeStmtResultMap: () => dummy_exe_stmt_result },
        })
        expect(wrapper.vm.stmtErrMsgObj).to.be.deep.equals(dummy_exe_stmt_result.stmt_err_msg_obj)
    })
})
describe(`SidebarContainer - button tests (If there is no connection)`, () => {
    let wrapper
    it(`Should disable reload-btn `, () => {
        wrapper = mountFactory({
            shallow: false,
            computed: { isConnecting: () => true },
        })
        expect(wrapper.find('.reload-btn').attributes().disabled).to.be.equals('disabled')
    })
    it(`Should disable filter-objects input`, () => {
        wrapper = mountFactory({
            shallow: false,
            computed: { isConnecting: () => true },
        })
        expect(
            wrapper
                .find('.filter-objects')
                .find('input')
                .attributes().disabled
        ).to.be.equals('disabled')
    })
    it(`Should call SET_IS_SIDEBAR_COLLAPSED when collapse-btn is clicked`, () => {
        let is_sidebar_collapsed = false
        wrapper = mountFactory({
            shallow: false,
            //SET_IS_SIDEBAR_COLLAPSED is a mutation, so it should be stubbed
            methods: { SET_IS_SIDEBAR_COLLAPSED: () => (is_sidebar_collapsed = true) },
        })
        wrapper.find('.collapse-btn').trigger('click')
        expect(is_sidebar_collapsed).to.be.true
    })
    it(`Should call reloadTreeNodes when reload button is clicked`, () => {
        wrapper = mountFactory({
            shallow: false,
            computed: { ...mockShowingDbListTree(), is_sidebar_collapsed: () => false },
        })
        const reloadTreeNodesSpy = sinon.spy(wrapper.vm, 'reloadTreeNodes')
        wrapper.find('.reload-btn').trigger('click')
        reloadTreeNodesSpy.should.have.been.calledOnce
    })
})
describe(`SidebarContainer - methods tests`, () => {
    let wrapper
    it(`Should process handleGetNodeData method as expected`, () => {
        let clearDataPreviewCallCount = 0
        let queryModeParam, fetchPrvwParams
        wrapper = mountFactory({
            methods: {
                clearDataPreview: () => clearDataPreviewCallCount++,
                SET_CURR_QUERY_MODE: mode => (queryModeParam = mode),
                fetchPrvw: params => (fetchPrvwParams = params),
            },
        })
        const mockParam = { SQL_QUERY_MODE: 'PRVW_DATA', schemaId: 'test.t1' }
        wrapper.vm.handleGetNodeData(mockParam)
        expect(clearDataPreviewCallCount).to.be.equals(1)
        expect(queryModeParam).to.be.equals(mockParam.SQL_QUERY_MODE)
        expect(fetchPrvwParams).to.be.deep.equals({
            tblId: mockParam.schemaId,
            prvwMode: mockParam.SQL_QUERY_MODE,
        })
    })
    it(`Should call updateTreeNodes when handleLoadChildren is called`, () => {
        let updateTreeNodesParam
        wrapper = mountFactory({
            methods: {
                updateTreeNodes: param => (updateTreeNodesParam = param),
            },
        })
        const mockNode = { key: 'node_key_20', type: 'Tables', name: 'Tables', id: 'test.Tables' }
        wrapper.vm.handleLoadChildren(mockNode)
        expect(updateTreeNodesParam).to.be.deep.equals(mockNode)
    })
    it(`Should process onAlterTable method as expected`, async () => {
        let queryTblCreationInfoParam
        wrapper = mountFactory({
            computed: {
                engines: () => [],
                charset_collation_map: () => new Map(),
                def_db_charset_map: () => new Map(),
            },
            methods: {
                queryTblCreationInfo: param => (queryTblCreationInfoParam = param),
            },
        })
        const mockNode = { key: 'node_key_20', type: 'Table', name: 't1', id: 'test.t1' }
        const fnsToBeSpied = ['queryEngines', 'queryCharsetCollationMap', 'queryDefDbCharsetMap']
        fnsToBeSpied.forEach(fn => {
            sinon.spy(wrapper.vm, fn)
        })

        await wrapper.vm.onAlterTable(mockNode) // trigger the method

        expect(queryTblCreationInfoParam).to.be.deep.equals(mockNode)
        wrapper.vm.queryEngines.should.have.been.calledOnce
        wrapper.vm.queryCharsetCollationMap.should.have.been.calledOnce
        wrapper.vm.queryDefDbCharsetMap.should.have.been.calledOnce
        // restore
        wrapper.vm.queryEngines.restore()
        wrapper.vm.queryCharsetCollationMap.restore()
        wrapper.vm.queryDefDbCharsetMap.restore()
    })
    it(`Should process onDropAction method as expected`, () => {
        wrapper = mountFactory()
        const mockNode = { type: 'Table', id: 'test.t1' }
        wrapper.vm.onDropAction(mockNode) // trigger the method
        expect(wrapper.vm.sql).to.be.equals('DROP TABLE `test`.`t1`;')
        expect(wrapper.vm.actionName).to.be.equals('DROP TABLE `test`.`t1`')
        expect(wrapper.vm.isExeDlgOpened).to.be.true
    })
    it(`Should process onTruncateTbl method as expected`, () => {
        wrapper = mountFactory()
        wrapper.vm.onTruncateTbl('test.t1') // trigger the method
        expect(wrapper.vm.sql).to.be.equals('truncate `test`.`t1`;')
        expect(wrapper.vm.actionName).to.be.equals('truncate `test`.`t1`')
        expect(wrapper.vm.isExeDlgOpened).to.be.true
    })
    it(`Should call exeStmtAction method when confirmExeStatements is called`, () => {
        const mockSql = 'truncate `test`.`t1`;'
        const mockActionName = 'truncate `test`.`t1`'
        wrapper = mountFactory({ data: () => ({ sql: mockSql, actionName: mockActionName }) })
        sinon.spy(wrapper.vm, 'exeStmtAction')
        wrapper.vm.confirmExeStatements() // trigger the method
        wrapper.vm.exeStmtAction.should.have.been.calledOnceWith({
            sql: mockSql,
            action: mockActionName,
        })
        wrapper.vm.exeStmtAction.restore()
    })
    it(`Should call UPDATE_EXE_STMT_RESULT_MAP mutation when
      clearExeStatementsResult is called`, () => {
        const mockActive_wke_id = 'wke_abcd'
        wrapper = mountFactory({
            computed: { active_wke_id: () => mockActive_wke_id },
            methods: { UPDATE_EXE_STMT_RESULT_MAP: () => null },
        })
        sinon.spy(wrapper.vm, 'UPDATE_EXE_STMT_RESULT_MAP')
        wrapper.vm.clearExeStatementsResult() // trigger the method
        wrapper.vm.UPDATE_EXE_STMT_RESULT_MAP.should.have.been.calledWith({ id: mockActive_wke_id })
        wrapper.vm.UPDATE_EXE_STMT_RESULT_MAP.restore()
    })
})
