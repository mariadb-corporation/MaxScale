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
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import SelectDropdown from '@/components/common/SelectDropdown'
import { itemSelectMock } from '@tests/unit/utils'

let multipleChoiceItems = [
    {
        id: 'RWS-Router',
        type: 'services',
    },
    {
        id: 'RCR-Writer',
        type: 'services',
    },
    {
        id: 'RCR-Router',
        type: 'services',
    },
]

let singleChoiceItems = [
    { id: 'Monitor-Test', type: 'monitors' },
    { id: 'Monitor', type: 'monitors' },
]

describe('SelectDropdown.vue', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: SelectDropdown,
            props: {
                // entityName is always plural by default, this makes translating process easier
                entityName: 'servers',
                items: [
                    {
                        attributes: { state: 'Down' },
                        id: 'test-server',
                        links: { self: 'https://127.0.0.1:8989/v1/servers/test-server' },
                        type: 'servers',
                    },
                ],
            },
        })
    })

    it(`Should render accurate placeholder when multiple props is false`, async () => {
        // get the wrapper div
        let placeholderWrapper = wrapper.find('.v-select__selections').html()
        // check include correct placeholder value
        expect(placeholderWrapper).to.be.include('placeholder="Select a server"')
    })
    it(`Should render accurate placeholder when multiple props is true`, async () => {
        await wrapper.setProps({
            multiple: true,
        })
        // get the wrapper div
        let placeholderWrapper = wrapper.find('.v-select__selections').html()
        // check include correct placeholder value
        expect(placeholderWrapper).to.be.include('placeholder="Select servers"')
    })
    it(`Should render empty placeholder when showPlaceHolder props is false`, async () => {
        await wrapper.setProps({
            showPlaceHolder: false,
        })
        let placeholderWrapper = wrapper.find('.v-select__selections').html()
        // check include correct placeholder value
        expect(placeholderWrapper).to.be.include('placeholder=""')
    })

    it(`Should add 'error--text__bottom' class when required props is true`, async () => {
        await wrapper.setProps({
            required: true,
        })
        expect(wrapper.find('.error--text__bottom').exists()).to.be.true
    })

    it(`Should render clear button when clearable props is true`, async () => {
        await wrapper.setProps({
            clearable: true,
            items: singleChoiceItems,
            defaultItems: singleChoiceItems[0],
        })
        expect(wrapper.find('.v-input__icon--clear').exists()).to.be.true
    })

    it(`Should render pre-selected item accurately when defaultItems props
      is passed with a valid object and multiple props is false`, async () => {
        await wrapper.setProps({
            entityName: 'monitors',
            multiple: false,
            items: singleChoiceItems,
            defaultItems: singleChoiceItems[0],
        })

        let preSelectedItem = wrapper.vm.$data.selectedItems
        expect(preSelectedItem).to.be.an('object')
        expect(preSelectedItem.id).to.be.equal(singleChoiceItems[0].id)
        const selectionSpans = wrapper.findAll('.v-select__selection')
        expect(selectionSpans.length).to.be.equal(1)
        expect(selectionSpans.at(0).html()).to.be.include(singleChoiceItems[0].id)
    })
    it(`Should render pre-selected items accurately when defaultItems props
      is passed with a valid array and multiple props is true`, async () => {
        await wrapper.setProps({
            entityName: 'services',
            multiple: true,
            items: multipleChoiceItems,
            defaultItems: [multipleChoiceItems[0], multipleChoiceItems[1]],
        })

        let preSelectedItems = wrapper.vm.$data.selectedItems
        expect(preSelectedItems).to.be.an('array')
        preSelectedItems.forEach((item, i) =>
            expect(item.id).to.be.equal(multipleChoiceItems[i].id)
        )
        const selectionSpans = wrapper.findAll('.v-select__selection')
        expect(selectionSpans.length).to.be.equal(2)
        expect(selectionSpans.at(0).html()).to.be.include(multipleChoiceItems[0].id)
        expect(selectionSpans.at(1).html()).to.be.include('(+1 others)')
    })

    it(`Should emit get-selected-items and return array
      when multiple props is true`, async () => {
        /* ---------------  Test get-selected-items event ------------------------ */
        await wrapper.setProps({
            entityName: 'services',
            multiple: true,
            items: multipleChoiceItems,
        })
        let chosenItems = []
        wrapper.vm.$on('get-selected-items', values => {
            chosenItems = values
        })

        // mockup onchange event when selecting items
        multipleChoiceItems.forEach(async item => {
            await itemSelectMock(wrapper, item)
        })

        expect(chosenItems).to.be.an('array')
        chosenItems.forEach((item, i) => {
            expect(item.id).to.be.equal(multipleChoiceItems[i].id)
        })
    })
    it(`Should emit get-selected-items and return array
      when multiple props is false`, async () => {
        /* ---------------  Test get-selected-items event ------------------------ */
        await wrapper.setProps({
            entityName: 'monitors',
            multiple: false,
            items: singleChoiceItems,
        })
        let chosenItems = []
        wrapper.vm.$on('get-selected-items', values => {
            chosenItems = values
        })
        // mockup onchange event when selecting an item
        await itemSelectMock(wrapper, singleChoiceItems[0])

        expect(chosenItems).to.be.an('array')
        expect(chosenItems.length).to.be.equal(1)
        chosenItems.forEach((item, i) => {
            expect(item.id).to.be.equal(singleChoiceItems[i].id)
        })
    })

    it(`Should emit has-changed event and return accurate value
      when multiple props is false`, async () => {
        /* ---------------  Test has-changed event when multiple select is enabled------------------------ */
        await wrapper.setProps({
            entityName: 'monitors',
            multiple: false,
            items: singleChoiceItems,
            defaultItems: singleChoiceItems[1],
        })
        let counter = 0
        /*It returns false if new selected items are not equal to defaultItems
          (aka pre selected items), else return true
        */
        wrapper.vm.$on('has-changed', bool => {
            counter++
            if (counter === 1) expect(bool).to.equal(false)
            if (counter === 2) expect(bool).to.equal(true)
        })
        // mockup onchange event when selecting item

        // Change to new item, has-changed should return false
        await itemSelectMock(wrapper, singleChoiceItems[0])
        /*
            Select original item has-changed should return true
            as current selected item is equal with defaultItems
        */
        await itemSelectMock(wrapper, singleChoiceItems[1])
    })
    it(`Should emit has-changed event and return accurate value
       when multiple props is true`, async () => {
        /* ---------------  Test has-changed event when multiple select is enabled------------------------ */
        await wrapper.setProps({
            entityName: 'services',
            multiple: true,
            items: multipleChoiceItems,
            defaultItems: [multipleChoiceItems[0]],
        })

        let counter = 0
        /*It returns false if new selected items are not equal to defaultItems
          (aka pre selected items), else return true
        */
        wrapper.vm.$on('has-changed', bool => {
            counter++
            if (counter === 1) expect(bool).to.equal(false)
            if (counter === 2) expect(bool).to.equal(true)
        })
        // mockup onchange event when selecting item

        // add new item, has-changed should return false
        await itemSelectMock(wrapper, multipleChoiceItems[1])
        /*
            unselect selected item, has-changed should return true
            as current selected items are equal with defaultItems
        */
        await itemSelectMock(wrapper, multipleChoiceItems[1])
    })
})
