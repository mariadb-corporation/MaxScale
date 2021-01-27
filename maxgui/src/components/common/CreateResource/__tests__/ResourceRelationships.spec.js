/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import ResourceRelationships from '@CreateResource/ResourceRelationships'
import { itemSelectMock } from '@tests/unit/utils'

const mockupResourceItems = [
    { id: 'test-server-0', type: 'servers' },
    { id: 'test-server-1', type: 'servers' },
    { id: 'test-server-2', type: 'servers' },
]
describe('ResourceRelationships.vue', () => {
    let wrapper

    beforeEach(async () => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: ResourceRelationships,
            props: {
                items: mockupResourceItems,
                required: false, // whether relationship needs to be defined or not
                relationshipsType: 'servers', // resource name, always plural word
                // control multiple selection and render label as plural or singular word
                multiple: false,
            },
        })
    })

    it(`Should render relationship name as singular word accurately
      when multiple props is false`, async () => {
        const label = wrapper.find('.collapse-title').text()
        expect(label).to.be.equals('server')
    })

    it(`Should render relationship name as singular word accurately
      when multiple props is true`, async () => {
        await wrapper.setProps({
            multiple: true,
            relationshipsType: 'services',
        })
        const label = wrapper.find('.collapse-title').text()
        expect(label).to.be.equals('services')
    })

    it(`Should show select-dropdown by default`, async () => {
        expect(wrapper.find('.collapse-content').attributes().style).to.be.undefined
    })

    it(`Should not show select-dropdown when arrow-toggle is clicked`, async () => {
        await wrapper.find('.arrow-toggle').trigger('click')
        expect(wrapper.find('.collapse-content').attributes().style).to.be.equals('display: none;')
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

    it(`Should pass the following props to select-dropdown`, () => {
        const selectDropdown = wrapper.findComponent({ name: 'select-dropdown' })
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
