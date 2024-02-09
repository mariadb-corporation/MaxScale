/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ExecuteSqlDialog from '@wsComps/ExecuteSqlDialog.vue'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: ExecuteSqlDialog,
                propsData: {
                    ctrDim: { width: 1000, height: 500 },
                },
            },
            opts
        )
    )
let wrapper
describe('execute-sql-dialog', () => {
    describe(`Created hook and child component's data communication tests`, () => {
        it(`Should pass accurate data to mxs-dlg via props`, () => {
            wrapper = mountFactory()
            const { value, title, hasSavingErr, onSave } = wrapper.findComponent({
                name: 'mxs-dlg',
            }).vm.$props
            expect(value).to.be.equals(wrapper.vm.isConfDlgOpened)
            expect(title).to.be.equals(
                wrapper.vm.$mxs_tc('confirmations.exeStatements', wrapper.vm.stmtI18nPluralization)
            )
            expect(hasSavingErr).to.be.equals(wrapper.vm.isExecFailed)
            expect(onSave).to.be.equals(wrapper.vm.exec_sql_dlg.on_exec)
        })

        it(`Should pass accurate data to mxs-sql-editor via props`, () => {
            wrapper = mountFactory({
                computed: {
                    isConfDlgOpened: () => true, // mock dialog opened
                },
            })
            const { value, completionItems, options, skipRegCompleters } = wrapper.findComponent({
                name: 'mxs-sql-editor',
            }).vm.$props
            expect(value).to.be.equals(wrapper.vm.currSql)
            expect(completionItems).to.be.equals(wrapper.vm.completionItems)
            expect(options).to.be.eql({ fontSize: 10, contextmenu: false, wordWrap: 'on' })
            expect(skipRegCompleters).to.be.equals(wrapper.vm.isSqlEditor)
        })

        it(`Should show small info`, () => {
            wrapper = mountFactory()
            expect(wrapper.find('small').text()).to.be.equals(
                wrapper.vm.$mxs_tc('info.exeStatementsInfo', wrapper.vm.stmtI18nPluralization)
            )
        })
    })
    describe(`Computed, method and other tests`, () => {
        it(`Should return accurate value for stmtI18nPluralization`, () => {
            // when sql is empty or has only a statement
            wrapper = mountFactory()
            expect(wrapper.vm.stmtI18nPluralization).to.be.equals(1)
            // when sql has more than one statement
            wrapper = mountFactory({
                computed: { exec_sql_dlg: () => ({ sql: 'SELET 1; SELLET 2; SELECT 3;' }) },
            })
            expect(wrapper.vm.stmtI18nPluralization).to.be.equals(2)
        })
    })
})
