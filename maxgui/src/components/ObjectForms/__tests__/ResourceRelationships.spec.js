/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ResourceRelationships from '@src/components/ObjectForms/ResourceRelationships'
import { itemSelectMock } from '@tests/unit/utils'

const mockupResourceItems = [
    { id: 'test-server-0', type: 'servers' },
    { id: 'test-server-1', type: 'servers' },
    { id: 'test-server-2', type: 'servers' },
]
describe('ResourceRelationships.vue', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: ResourceRelationships,
            propsData: {
                items: mockupResourceItems,
                required: false, // whether relationship needs to be defined or not
                relationshipsType: 'servers', // resource name, always plural word
                // control multiple selection and render label as plural or singular word
                multiple: false,
            },
        })
    })

    it(`Should render relationship name as singular word accurately
      when multiple props is false`, () => {
        const label = wrapper.find('.collapsible-ctr-title').text()
        expect(label).to.be.equals('server')
    })

    it(`Should render relationship name as singular word accurately
      when multiple props is true`, async () => {
        await wrapper.setProps({
            multiple: true,
            relationshipsType: 'services',
        })
        const label = wrapper.find('.collapsible-ctr-title').text()
        expect(label).to.be.equals('services')
    })

    it(`Should show mxs-select by default`, () => {
        expect(wrapper.find('.collapsible-ctr-content').attributes().style).to.be.undefined
    })

    it(`Should not show mxs-select when arrow-toggle is clicked`, async () => {
        await wrapper.find('.arrow-toggle').trigger('click')
        expect(wrapper.find('.collapsible-ctr-content').attributes().style).to.be.equals(
            'display: none;'
        )
    })

    it(`Multiple mode off: Should return selectedItems as an array
      when getSelectedItems method get called`, async () => {
        // mockup selecting a server
        await itemSelectMock(wrapper, mockupResourceItems[0])

        expect(wrapper.vm.getSelectedItems()).to.be.an('array')
        expect(wrapper.vm.getSelectedItems()).to.be.deep.equals([mockupResourceItems[0]])
    })

    it(`Multiple mode on: Should return selectedItems as an array
      when getSelectedItems method get called`, async () => {
        await wrapper.setProps({
            multiple: true,
        })
        // mockup selecting multiple servers
        mockupResourceItems.forEach(async item => await itemSelectMock(wrapper, item))

        expect(wrapper.vm.getSelectedItems()).to.be.an('array')
        expect(wrapper.vm.getSelectedItems()).to.be.deep.equals(mockupResourceItems)
    })

    it(`Should pass the following props to mxs-select`, () => {
        const selectDropdown = wrapper.findComponent({ name: 'mxs-select' })
        const {
            entityName,
            items,
            defaultItems,
            multiple,
            clearable,
            required,
        } = selectDropdown.vm.$props

        const {
            relationshipsType,
            items: wrapperItems,
            defaultItems: wrapperDefaultItems,
            multiple: wrapperMultiple,
            clearable: wrapperClearable,
            required: wrapperRequired,
        } = wrapper.vm.$props

        expect(entityName).to.be.equals(relationshipsType)
        expect(items).to.be.deep.equals(wrapperItems)
        expect(defaultItems).to.be.deep.equals(wrapperDefaultItems)
        expect(multiple).to.be.equals(wrapperMultiple)
        expect(clearable).to.be.equals(wrapperClearable)
        expect(required).to.be.equals(wrapperRequired)
    })
})
