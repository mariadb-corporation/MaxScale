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
import MigrDeleteDlg from '@wkeComps/DataMigration/MigrDeleteDlg'
import { MIGR_DLG_TYPES } from '@wsSrc/constants'
import { lodash } from '@share/utils/helpers'
import { task } from '@wkeComps/DataMigration/__tests__/stubData'
import EtlTask from '@wsModels/EtlTask'
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'

const mountFactory = opts => mount(lodash.merge({ shallow: true, component: MigrDeleteDlg }, opts))

describe('MigrDeleteDlg', () => {
    let wrapper
    afterEach(() => sinon.restore())
    it('Should pass accurate data to mxs-dlg', () => {
        wrapper = mountFactory()
        const { value, onSave, title, saveText } = wrapper.findComponent({
            name: 'mxs-dlg',
        }).vm.$props
        expect(value).to.equal(wrapper.vm.isOpened)
        expect(onSave).to.eql(wrapper.vm.onSave)
        expect(title).to.equal(wrapper.vm.$mxs_t('confirmations.deleteEtl'))
        expect(saveText).to.equal(wrapper.vm.migr_dlg.type)
    })

    Object.values(MIGR_DLG_TYPES).forEach(type => {
        const shouldBeOpened = type === MIGR_DLG_TYPES.DELETE
        it(`Should ${shouldBeOpened ? 'open' : 'not open'} the dialog
        when migr_dlg type is ${type}`, () => {
            wrapper = mountFactory({ computed: { migr_dlg: () => ({ type, is_opened: true }) } })
            expect(wrapper.vm.isOpened).to.equal(shouldBeOpened)
        })
    })

    it(`Should call SET_MIGR_DLG when isOpened value is changed`, () => {
        wrapper = mountFactory()
        const stub = sinon.stub(wrapper.vm, 'SET_MIGR_DLG')
        const newValue = !wrapper.vm.isOpened
        wrapper.vm.isOpened = newValue
        stub.calledOnceWithExactly({ ...wrapper.vm.migr_dlg, is_opened: newValue })
    })

    it(`Should handle onSave method as expected`, async () => {
        const mockWke = { id: 'wke-id' }
        wrapper = mountFactory({
            computed: {
                migr_dlg: () => ({
                    type: MIGR_DLG_TYPES.DELETE,
                    is_opened: true,
                    etl_task_id: task.id,
                }),
                etlTaskWke: () => mockWke,
            },
        })
        await wrapper.vm.onSave()

        const queryConnDispatchStub = sinon.stub(QueryConn, 'dispatch')
        const worksheetDispatchStub = sinon.stub(Worksheet, 'dispatch')
        const etlTaskDispatchStub = sinon.stub(EtlTask, 'dispatch')

        queryConnDispatchStub.calledOnceWithExactly('disconnectConnsFromTask', task.id)
        worksheetDispatchStub.calledOnceWithExactly('handleDeleteWke', mockWke.id)
        etlTaskDispatchStub.calledOnceWithExactly('cascadeDelete', task.id)
    })
})
