/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import chai, { expect } from 'chai'
import sinon from 'sinon'
import mount from '@tests/unit/setup'
import OverviewHeader from '@/pages/MonitorDetail/OverviewHeader'
import { dummy_all_monitors } from '@tests/unit/utils'
import sinonChai from 'sinon-chai'

chai.should()
chai.use(sinonChai)

/**
 * @param {Object} wrapper - mounted component
 */
async function mockOpenSwitchOverDialog(wrapper) {
    await wrapper.setData({
        showEditBtn: true,
    })
    const switchOverBtn = wrapper.find('.switchover-edit-btn')
    await switchOverBtn.trigger('click')
}

const selectedItemsStub = [{ id: 'test-server', type: 'servers' }]
describe('OverviewHeader index', () => {
    let wrapper
    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: OverviewHeader,
            props: {
                currentMonitor: dummy_all_monitors[0],
            },
        })
    })

    it(`Should render four outlined-overview-card components`, async () => {
        const outlineOverviewCards = wrapper.findAllComponents({ name: 'outlined-overview-card' })
        expect(outlineOverviewCards.length).to.be.equals(4)
    })

    it(`Should automatically assign 'undefined' string if attribute is not defined`, async () => {
        let currentMonitor = wrapper.vm.$help.lodash.cloneDeep(dummy_all_monitors[0])
        currentMonitor.attributes.monitor_diagnostics = {}
        await wrapper.setProps({
            currentMonitor: currentMonitor,
        })
        const getTopOverviewInfo = wrapper.vm.getTopOverviewInfo
        Object.values(getTopOverviewInfo).forEach(value => expect(value).to.be.equals('undefined'))
    })

    it(`Should shows master, master_gtid_domain_id, state, primary value`, async () => {
        const expectKeys = ['master', 'master_gtid_domain_id', 'state', 'primary']
        const getTopOverviewInfo = wrapper.vm.getTopOverviewInfo
        expect(Object.keys(getTopOverviewInfo)).to.be.deep.equals(expectKeys)
    })

    it(`Should show switchover-edit-btn when "Master" block is hovered`, async () => {
        expect(wrapper.vm.$data.showEditBtn).to.be.false
        const cardMaster = wrapper.find('.card-master')
        await cardMaster.trigger('mouseenter')
        expect(wrapper.vm.$data.showEditBtn).to.be.true
    })

    it(`Should open select-dialog when switchover-edit-btn is clicked`, async () => {
        expect(wrapper.vm.$data.showSelectDialog).to.be.false
        await mockOpenSwitchOverDialog(wrapper)
        expect(wrapper.vm.$data.showSelectDialog).to.be.true
    })

    it(`Should pass necessary props to select-dialog`, async () => {
        const selectDialog = wrapper.findComponent({ name: 'select-dialog' })

        const {
            title,
            mode,
            entityName,
            onClose,
            onCancel,
            handleSave,
            itemsList,
            defaultItems,
        } = selectDialog.vm.$props
        const {
            $data: {
                dialogTitle,
                targetSelectItemType,
                itemsList: itemsListData,
                defaultItems: defaultItemsData,
            },
            handleClose,
            confirmChange,
        } = wrapper.vm
        expect(title).to.be.equals(dialogTitle)
        expect(mode).to.be.equals('swap')
        expect(entityName).to.be.equals(targetSelectItemType)
        expect(onClose).to.be.equals(handleClose)
        expect(onCancel).to.be.equals(handleClose)
        expect(handleSave).to.be.equals(confirmChange)
        expect(itemsList).to.be.deep.equals(itemsListData)
        expect(defaultItems).to.be.deep.equals(defaultItemsData)
    })

    it(`Should pass event handler for @on-open event of select-dialog`, async () => {
        let getAllEntitiesSpy = sinon.spy(wrapper.vm, 'getAllEntities')
        await mockOpenSwitchOverDialog(wrapper)
        getAllEntitiesSpy.should.have.been.calledOnce
    })

    it(`Should update targetItem data when @selected-items event of select-dialog
      is emitted`, async () => {
        const selectDialog = wrapper.findComponent({ name: 'select-dialog' })
        await mockOpenSwitchOverDialog(wrapper)

        selectDialog.vm.$emit('selected-items', selectedItemsStub)
        expect(wrapper.vm.$data.targetItem).to.be.deep.equals(selectedItemsStub)
    })

    it(`Should emit @switch-over event with new master id`, async () => {
        wrapper.vm.$on('switch-over', newMasterId => {
            expect(newMasterId).to.be.equals(selectedItemsStub[0].id)
        })
        await wrapper.setData({
            targetItem: selectedItemsStub,
        })
        await wrapper.vm.confirmChange()
    })
})
