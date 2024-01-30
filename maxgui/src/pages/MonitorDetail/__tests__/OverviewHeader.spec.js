/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import OverviewHeader from '@rootSrc/pages/MonitorDetail/OverviewHeader'
import { dummy_all_monitors } from '@tests/unit/utils'

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
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: OverviewHeader,
            propsData: {
                currentMonitor: dummy_all_monitors[0],
            },
        })
    })

    it(`Should render four outlined-overview-card components`, () => {
        const outlineOverviewCards = wrapper.findAllComponents({ name: 'outlined-overview-card' })
        expect(outlineOverviewCards.length).to.be.equals(4)
    })

    it(`Should automatically assign 'undefined' string if attribute is not defined`, async () => {
        let currentMonitor = wrapper.vm.$helpers.lodash.cloneDeep(dummy_all_monitors[0])
        currentMonitor.attributes.monitor_diagnostics = {}
        await wrapper.setProps({
            currentMonitor: currentMonitor,
        })
        const getTopOverviewInfo = wrapper.vm.getTopOverviewInfo
        Object.values(getTopOverviewInfo).forEach(value => expect(value).to.be.equals('undefined'))
    })

    it(`Should shows master, master_gtid_domain_id, state, primary value`, () => {
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

    it(`Should open sel-dlg when switchover-edit-btn is clicked`, async () => {
        const selectDialog = wrapper.findComponent({ name: 'sel-dlg' })
        expect(selectDialog.vm.$attrs.value).to.be.false
        await mockOpenSwitchOverDialog(wrapper)
        expect(selectDialog.vm.$attrs.value).to.be.true
    })

    it(`Should pass necessary props to sel-dlg`, () => {
        const selectDialog = wrapper.findComponent({ name: 'sel-dlg' })

        const { entityName, itemsList, defaultItems } = selectDialog.vm.$props
        const { title, saveText, onSave } = selectDialog.vm.$attrs
        const {
            $data: {
                dialogTitle,
                targetSelectItemType,
                itemsList: itemsListData,
                defaultItems: defaultItemsData,
            },
            confirmChange,
        } = wrapper.vm
        expect(title).to.be.equals(dialogTitle)
        expect(saveText).to.be.equals('swap')
        expect(entityName).to.be.equals(targetSelectItemType)
        expect(onSave).to.be.equals(confirmChange)
        expect(itemsList).to.be.deep.equals(itemsListData)
        expect(defaultItems).to.be.deep.equals(defaultItemsData)
    })

    it(`Should pass event handler for @on-open event of sel-dlg`, async () => {
        let getAllEntitiesSpy = sinon.spy(wrapper.vm, 'getAllEntities')
        await mockOpenSwitchOverDialog(wrapper)
        getAllEntitiesSpy.should.have.been.calledOnce
    })

    it(`Should update targetItem data when @selected-items event of sel-dlg
      is emitted`, async () => {
        const selectDialog = wrapper.findComponent({ name: 'sel-dlg' })
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
