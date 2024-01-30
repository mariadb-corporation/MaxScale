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
import RelationshipTable from '@rootSrc/components/DetailsPage/RelationshipTable'
import { serviceStateTableRowsStub, getFilterListStub, itemSelectMock } from '@tests/unit/utils'

/**
 * This function mockups opening confirmation dialog when clicking
 * delete icon in data-table.
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String|Number} id id of item to be deleted
 */
async function mockupOpenConfirmDeletingDialog(wrapper, id) {
    const dataTable = wrapper.findComponent({ name: 'data-table' })
    const tbody = dataTable.find(`tbody`)
    const { wrappers: tableRowEls } = tbody.findAll('tr')
    let index = tableRowEls.findIndex(tr => tr.html().includes(id))
    await tableRowEls[index].trigger('mouseenter')
    let deleteBtn = tableRowEls[index].find('button')
    await deleteBtn.trigger('click')
}

/**
 * This function mockups opening sel-dlg when clicking
 * add button in collapsible-ctr component
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
async function mockupOpenSelectDialog(wrapper) {
    const mxsCollapse = wrapper.findComponent({ name: 'collapsible-ctr' })
    let addBtn = mxsCollapse.find('.add-btn')
    await addBtn.trigger('click')
}
/**
 * Manually triggering this method as it is triggered once
 * when component is mounted. Also, there is no reason for
 * relationshipType to be changed after component is mounted,
 * hence there's no need to have a watcher for relationshipType
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
async function mockupRelationshipTypeWatcher(wrapper, relationshipType) {
    await wrapper.vm.assignTableHeaders(relationshipType)
}

const dummyAllServicesState = [
    {
        attributes: {
            state: 'Started',
        },
        id: 'service_0',
        type: 'services',
    },
    {
        attributes: {
            state: 'Started',
        },
        id: 'service_1',
        type: 'services',
    },
    {
        attributes: {
            state: 'Started',
        },
        id: 'RWS-Router',
        type: 'services',
    },
    {
        attributes: {
            state: 'Started',
        },
        id: 'RCR-Router',
        type: 'services',
    },
    {
        attributes: {
            state: 'Started',
        },
        id: 'RCR-Writer',
        type: 'services',
    },
]

describe('RelationshipTable.vue without removable and addable ability', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: RelationshipTable,
            propsData: {
                relationshipType: 'services',
                tableRows: serviceStateTableRowsStub,
                removable: false,
                addable: false,
            },
            computed: {
                isLoading: () => false,
            },
        })
    })

    it(`Should not render 'add button'`, () => {
        expect(wrapper.find('.add-btn').exists()).to.be.false
    })

    it(`Should not render confirm-dlg and sel-dlg components`, () => {
        expect(wrapper.findComponent({ name: 'confirm-dlg' }).exists()).to.be.false
        expect(wrapper.findComponent({ name: 'sel-dlg' }).exists()).to.be.false
    })

    it(`Should render router-link components and assign accurate target location`, () => {
        const { wrappers: routerLinks } = wrapper.findAllComponents({ name: 'router-link' })
        expect(routerLinks.length).to.be.equals(serviceStateTableRowsStub.length)
        routerLinks.forEach((routerLink, i) => {
            const aTag = routerLink.find('a')
            expect(aTag.attributes().href).to.be.equals(
                `#/dashboard/services/${serviceStateTableRowsStub[i].id}`
            )
        })
    })

    it(`Should add index to each table row when relationshipType === 'filters'`, async () => {
        await wrapper.vm.tableRowsData.forEach(row => {
            expect(row).to.not.have.property('index')
        })
        await wrapper.setProps({
            relationshipType: 'filters',
        })
        await mockupRelationshipTypeWatcher(wrapper, 'filters')
        const tableRowsData = wrapper.vm.tableRowsData
        expect(tableRowsData).to.be.an('array')
        expect(tableRowsData.length).to.be.equals(serviceStateTableRowsStub.length)
        await tableRowsData.forEach((row, i) => {
            expect(row).to.have.property('index')
            expect(row.index).to.be.equals(i)
        })
    })

    it(`Should use filter table headers when relationshipType === 'filters'`, async () => {
        await wrapper.setProps({
            relationshipType: 'filters',
        })
        await mockupRelationshipTypeWatcher(wrapper, 'filters')
        const expectTableHeaders = [
            {
                text: '',
                value: 'index',
                width: '1px',
                padding: '0px 8px',
                sortable: false,
            },
            { text: 'Filter', value: 'id', sortable: false },
            { text: '', value: 'action', sortable: false },
        ]
        expect(wrapper.vm.tableHeader).to.be.deep.equals(expectTableHeaders)
    })
})

const getRelationshipDataStub = () => dummyAllServicesState

describe('RelationshipTable.vue with removable and addable ability', () => {
    let wrapper, getRelationshipDataSpy
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: RelationshipTable,
            propsData: {
                relationshipType: 'services',
                tableRows: serviceStateTableRowsStub,
                removable: true,
                addable: true,
                getRelationshipData: getRelationshipDataStub,
            },
            computed: {
                isLoading: () => false,
            },
        })
        getRelationshipDataSpy = sinon.spy(getRelationshipDataStub)
    })

    it(`Should open confirm-dlg when delete button is clicked`, async () => {
        await mockupOpenConfirmDeletingDialog(wrapper, serviceStateTableRowsStub[0].id)
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dlg' })
        expect(confirmDialog.vm.$attrs.value).to.be.true
    })

    it(`Should display accurate delete dialog title`, async () => {
        await mockupOpenConfirmDeletingDialog(wrapper, serviceStateTableRowsStub[0].id)
        const title = wrapper.find('.v-card__title>h3')
        expect(title.text()).to.be.equals(
            `${wrapper.vm.$mxs_t('unlink')} ${wrapper.vm.$mxs_tc('services', 1)}`
        )
    })

    it(`Should emit on-relationship-update event after confirm deleting`, async () => {
        const idToBeDeleted = serviceStateTableRowsStub[0].id
        let count = 0
        wrapper.vm.$on('on-relationship-update', ({ type, data }) => {
            expect(data)
                .to.be.an('array')
                .that.does.not.include({ id: idToBeDeleted, type: 'services' })
            data.forEach(item => {
                expect(item).to.be.an('object')
                expect(item).to.be.have.all.keys('id', 'type')
            })

            expect(type).to.be.equals('services')
            ++count
        })
        await mockupOpenConfirmDeletingDialog(wrapper, idToBeDeleted)
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dlg' })
        const saveBtn = confirmDialog.find('.save')
        await saveBtn.trigger('click')
        expect(count).to.be.equals(1)
    })

    it(`Should open sel-dlg when add button is clicked`, async () => {
        await mockupOpenSelectDialog(wrapper)
        const selectDialog = wrapper.findComponent({ name: 'sel-dlg' })
        expect(selectDialog.vm.$attrs.value).to.be.true
    })

    it(`Should display accurate select dialog title`, async () => {
        await mockupOpenSelectDialog(wrapper)
        const title = wrapper.find('.v-card__title>h3')
        expect(title.text()).to.be.equals(
            `${wrapper.vm.$mxs_t(`addEntity`, {
                entityName: wrapper.vm.$mxs_tc('services', 2),
            })}`
        )
    })

    it(`Should call getRelationshipData function props to get relationship state
      of all resources and display only resources are not in the table`, async () => {
        await mockupOpenSelectDialog(wrapper)
        await wrapper.vm.$nextTick(
            async () => await getRelationshipDataSpy.should.have.been.calledOnce
        )
        wrapper.vm.itemsList.forEach(item => {
            serviceStateTableRowsStub.forEach(row => {
                expect(item.id !== row.id).to.be.true
            })
        })
    })

    it(`Should call getRelationshipData function props and use selectItems props
      to show available items to be selected`, async () => {
        const selectItems = [{ id: 'test-service', state: 'Started', type: 'services' }]
        await wrapper.setProps({
            selectItems: selectItems,
        })
        await mockupOpenSelectDialog(wrapper)
        await wrapper.vm.$nextTick(
            async () => await getRelationshipDataSpy.should.have.been.calledOnce
        )
        expect(wrapper.vm.itemsList).to.be.deep.equals(selectItems)
    })

    it(`Should emit on-relationship-update event after confirm adding`, async () => {
        await mockupOpenSelectDialog(wrapper)
        let itemToBeAdded = wrapper.vm.itemsList[0]
        let eventParam = {}
        wrapper.vm.$on('on-relationship-update', param => (eventParam = param))
        const selectDialog = wrapper.findComponent({ name: 'sel-dlg' })
        await itemSelectMock(selectDialog, itemToBeAdded)
        const saveBtn = selectDialog.find('.save')
        await saveBtn.trigger('click')
        await wrapper.vm.$nextTick()
        expect(
            eventParam.data.some(item =>
                wrapper.vm.$helpers.lodash.isEqual(item, {
                    id: itemToBeAdded.id,
                    type: itemToBeAdded.type,
                })
            )
        ).to.be.true
        eventParam.data.forEach(item => {
            expect(item).to.be.an('object')
            expect(item).to.be.have.all.keys('id', 'type')
        })
        expect(eventParam.type).to.be.equals('services')
    })

    it(`Should emit on-relationship-update event when changing filters order`, async () => {
        await wrapper.setProps({
            tableRows: getFilterListStub(),
            relationshipType: 'filters',
        })
        await mockupRelationshipTypeWatcher(wrapper, 'filters')

        const firstRow = wrapper.vm.tableRowsData[0]
        const secondRow = wrapper.vm.tableRowsData[1]
        let eventParam = {}
        wrapper.vm.$on('on-relationship-update', param => (eventParam = param))

        const dataTable = wrapper.findComponent({ name: 'data-table' })
        // mockup drag end event from data-table, drag first row to second row
        await dataTable.vm.$emit('on-drag-end', { oldIndex: 0, newIndex: 1 })
        await wrapper.vm.$nextTick()
        expect(eventParam.isFilterDrag).to.be.true
        expect(eventParam.data.length).to.be.equals(getFilterListStub().length)
        eventParam.data.forEach((item, i) => {
            expect(item).to.be.an('object')
            expect(item).to.be.have.all.keys('id', 'type')
            if (i === 0) expect(item.id).to.be.equals(secondRow.id)
            if (i === 1) expect(item.id).to.be.equals(firstRow.id)
        })
        expect(eventParam.type).to.be.equals('filters')
    })
})
