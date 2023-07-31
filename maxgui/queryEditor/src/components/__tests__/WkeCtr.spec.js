/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import WkeCtr from '../WkeCtr.vue'
import { lodash } from '@share/utils/helpers'

const dummyCtrDim = { width: 1280, height: 800 }
const dummy_query_sessions = [{ id: 'SESSION_123_45' }]
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: WkeCtr,
                propsData: {
                    ctrDim: dummyCtrDim,
                },
                computed: {
                    query_sessions: () => dummy_query_sessions,
                    getActiveSessionId: () => dummy_query_sessions[0].id,
                },
                stubs: {
                    'sql-editor': "<div class='stub'></div>",
                    'txt-editor-toolbar-ctr': "<div class='stub'></div>",
                },
            },
            opts
        )
    )
describe('wke-ctr', () => {
    describe(`Created hook and child component's data communication tests`, () => {
        let wrapper
        it(`Should pass accurate data to mxs-split-pane via props`, () => {
            wrapper = mountFactory()
            const { value, minPercent, split, disable } = wrapper.findComponent({
                name: 'mxs-split-pane',
            }).vm.$props
            expect(value).to.be.equals(wrapper.vm.$data.sidebarPct)
            expect(minPercent).to.be.equals(wrapper.vm.minSidebarPct)
            expect(split).to.be.equals('vert')
            expect(disable).to.be.equals(wrapper.vm.is_sidebar_collapsed)
        })

        it(`Should pass accurate data to execute-sql-dialog via props`, () => {
            wrapper = mountFactory({ isExecFailed: () => false })
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
            expect(value).to.be.equals(wrapper.vm.$data.execSqlDlg.isOpened)
            expect(title).to.be.equals(
                wrapper.vm.$mxs_tc('confirmations.exeStatements', wrapper.vm.stmtI18nPluralization)
            )
            expect(smallInfo).to.be.equals(
                wrapper.vm.$mxs_tc('info.exeStatementsInfo', wrapper.vm.stmtI18nPluralization)
            )
            expect(hasSavingErr).to.be.equals(wrapper.vm.isExecFailed)
            expect(errMsgObj).to.be.deep.equals(wrapper.vm.stmtErrMsgObj)
            expect(sqlTobeExecuted).to.be.equals(wrapper.vm.$data.execSqlDlg.sql)
            expect(onSave).to.be.equals(wrapper.vm.$data.execSqlDlg.onExec)
        })

        const fnEvtMap = {
            placeToEditor: 'place-to-editor',
            draggingTxt: 'on-dragging',
            dropTxtToEditor: 'on-dragend',
        }
        Object.keys(fnEvtMap).forEach(key => {
            it(`Should call ${key} if ${fnEvtMap[key]} event is emitted from sidebar-ctr`, () => {
                wrapper = mountFactory({ shallow: false, computed: { getIsTxtEditor: () => true } })
                const spyFn = sinon.spy(
                    wrapper.vm.$typy(wrapper.vm.$refs, 'editor[0]').safeObject,
                    key
                )
                const sidebar = wrapper.findComponent({ name: 'sidebar-ctr' })
                let param
                switch (key) {
                    case 'placeToEditor':
                        param = '`test`'
                        break
                    case 'draggingTxt':
                    case 'dropTxtToEditor':
                        param = 'event'
                        break
                }
                sidebar.vm.$emit(fnEvtMap[key], param)
                spyFn.should.have.been.calledOnceWith(param)
                spyFn.restore()
            })
        })
    })
    const editorModes = ['TXT_EDITOR', 'DDL_EDITOR']
    editorModes.forEach(mode => {
        let wrapper
        describe(`${mode} mode: child component's data communication tests`, () => {
            beforeEach(() => {
                wrapper = mountFactory({
                    computed: { getIsTxtEditor: () => (mode === 'TXT_EDITOR' ? true : false) },
                })
            })
            const renCom = mode === 'TXT_EDITOR' ? 'txt-editor-ctr' : 'ddl-editor-ctr'
            const hiddenCom = mode === 'TXT_EDITOR' ? 'ddl-editor-ctr' : 'txt-editor-ctr'
            it(`Should render ${renCom}`, () => {
                const editor = wrapper.findAllComponents({ name: renCom }).at(0)
                expect(editor.exists()).to.be.true
            })
            it(`Should pass accurate data to ${renCom} via props`, () => {
                const { dim } = wrapper.findAllComponents({ name: renCom }).at(0).vm.$props
                expect(dim).to.be.deep.equals(wrapper.vm.editorDim)
            })
            it(`Should not render ${hiddenCom}`, () => {
                expect(wrapper.findAllComponents({ name: hiddenCom }).length).to.be.equals(0)
            })
        })
    })
    describe(`Computed, method and other tests`, () => {
        let wrapper
        const is_sidebar_collapsed_values = [false, true]
        is_sidebar_collapsed_values.forEach(v => {
            describe(`When sidebar is${v ? '' : ' not'} collapsed`, () => {
                it(`Should return accurate value for minSidebarPct`, () => {
                    wrapper = mountFactory({ computed: { is_sidebar_collapsed: () => v } })
                    const minValueInPx = v ? 40 : 200
                    const minPct = (minValueInPx / dummyCtrDim.width) * 100
                    expect(wrapper.vm.minSidebarPct).to.be.equals(minPct)
                })
                it(`Should assign accurate value for sidebarPct`, () => {
                    wrapper = mountFactory({ computed: { is_sidebar_collapsed: () => v } })
                    const defValue = 240
                    wrapper.vm.handleSetSidebarPct()
                    // if is_sidebar_collapsed is true, use minSidebarPct value
                    const sidebarPct = v
                        ? wrapper.vm.minSidebarPct
                        : (defValue / dummyCtrDim.width) * 100
                    expect(wrapper.vm.sidebarPct).to.be.equals(sidebarPct)
                })
            })

            it(`Should return accurate value for stmtI18nPluralization`, () => {
                // when sql is empty or has only a statement
                wrapper = mountFactory()
                expect(wrapper.vm.stmtI18nPluralization).to.be.equals(1)
                // when sql has more than one statement
                wrapper = mountFactory({
                    data: () => ({ execSqlDlg: { sql: 'SELET 1; SELLET 2; SELECT 3;' } }),
                })
                expect(wrapper.vm.stmtI18nPluralization).to.be.equals(2)
            })

            const dummy_exe_stmt_result = {
                stmt_err_msg_obj: {
                    errno: 1064,
                    message: 'dummy message',
                    sqlstate: '42000',
                },
            }

            it(`Should return accurate value for stmtErrMsgObj`, () => {
                // When there is no statement has been executed yet
                wrapper = mountFactory()
                expect(wrapper.vm.stmtErrMsgObj).to.be.an('object').and.be.empty
                // When there is an error message
                wrapper = mountFactory({
                    computed: { getExeStmtResultMap: () => dummy_exe_stmt_result },
                })
                expect(wrapper.vm.stmtErrMsgObj).to.be.deep.equals(
                    dummy_exe_stmt_result.stmt_err_msg_obj
                )
            })

            it(`Should return accurate value for isExecFailed`, () => {
                // When there is no statement has been executed yet
                wrapper = mountFactory()
                expect(wrapper.vm.isExecFailed).to.be.false
                // When there is an error message
                wrapper = mountFactory({
                    computed: { getExeStmtResultMap: () => dummy_exe_stmt_result },
                })
                expect(wrapper.vm.isExecFailed).to.be.true
            })
        })
    })
})
