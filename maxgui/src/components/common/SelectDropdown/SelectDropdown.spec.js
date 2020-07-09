/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import SelectDropdown from '@/components/common/SelectDropdown'

describe('SelectDropdown.vue', () => {
    let wrapper

    beforeEach(() => {
        localStorage.clear()
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

    it(`Testing non-required props:
      - Component renders accurate placeholder value in terms of plural or singular text.
      - Component add 'error--text__bottom' class when required props is true
      - Component renders pre-selected item or pre-selected items when defaultItems has
        valid object or array`, async () => {
        // get the wrapper div
        let placeholderWrapper = wrapper.find('.v-select__selections').html()
        // check include correct placeholder value
        expect(placeholderWrapper).to.be.include('placeholder="Select a server"')
        /* ---------------  Test multiple props ------------------------ */
        await wrapper.setProps({
            multiple: true,
        })

        placeholderWrapper = wrapper.find('.v-select__selections').html()
        // check include correct placeholder value
        expect(placeholderWrapper).to.be.include('placeholder="Select servers"')

        /* ---------------  Test showPlaceHolder props ------------------------ */
        await wrapper.setProps({
            showPlaceHolder: false,
        })

        placeholderWrapper = wrapper.find('.v-select__selections').html()
        // check include correct placeholder value
        expect(placeholderWrapper).to.be.include('placeholder=""')

        /* ---------------  Test required props ------------------------ */
        await wrapper.setProps({
            required: true,
        })
        expect(wrapper.find('.error--text__bottom').exists()).to.be.true

        /* ---------------  Test defaultItems props ------------------------ */
        await wrapper.setProps({
            entityName: 'monitors',
            multiple: false,
            items: [
                { id: 'Monitor', type: 'monitors' },
                { id: 'Monitor-test', type: 'monitors' },
            ],
            /*
          defaultItems aka pre-select item, if multiple is true,
          defaultItems should be an array
        */
            defaultItems: { id: 'Monitor', type: 'monitors' },
        })
        expect(wrapper.vm.$data.selectedItems).to.be.an('object')
        expect(wrapper.vm.$data.selectedItems.id).to.be.equal('Monitor')
    })

    it(`Testing get-selected-items event:
      - get-selected-items always returns Array regardless multiple props is true or false
       `, async () => {
        /* ---------------  Test get-selected-items event ------------------------ */
        await wrapper.setProps({
            entityName: 'services',
            multiple: true,
            items: [
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
            ],
        })
        let chosenItems = []
        wrapper.vm.$on('get-selected-items', values => {
            chosenItems = values
        })

        // mockup onchange event when selecting item
        const vSelect = wrapper.findComponent({ name: 'v-select' })
        vSelect.vm.selectItem({
            id: 'RWS-Router',
            type: 'services',
        })

        expect(chosenItems).to.be.an('array')
        expect(chosenItems[0].id).to.be.equal('RWS-Router')
    })

    it(`Testing is-equal event when multiple props is false`, async () => {
        /* ---------------  Test is-equal event when multiple select is enabled------------------------ */
        await wrapper.setProps({
            entityName: 'monitors',
            multiple: false,
            items: [
                { id: 'Monitor-Test', type: 'monitors' },
                { id: 'Monitor', type: 'monitors' },
            ],
            defaultItems: { id: 'Monitor', type: 'monitors' },
        })
        let counter = 0
        /*It returns false if new selected items are not equal to defaultItems
          (aka pre selected items), else return true
        */
        wrapper.vm.$on('is-equal', bool => {
            counter++
            if (counter === 1) expect(bool).to.equal(false)
            if (counter === 2) expect(bool).to.equal(true)
        })
        // mockup onchange event when selecting item
        const vSelect = wrapper.findComponent({ name: 'v-select' })
        // add new item, is-equal should return false
        await vSelect.vm.selectItem({ id: 'Monitor-Test', type: 'monitors' })
        /*
            unselect selected item, is-equal should return true
            as current selected items are equal with defaultItems
        */
        await vSelect.vm.selectItem({ id: 'Monitor', type: 'monitors' })
    })

    it(`Testing is-equal event when multiple props is true`, async () => {
        /* ---------------  Test is-equal event when multiple select is enabled------------------------ */
        await wrapper.setProps({
            entityName: 'services',
            multiple: true,
            items: [
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
            ],
            defaultItems: [
                {
                    id: 'RWS-Router',
                    type: 'services',
                },
            ],
        })

        let counter = 0
        /*It returns false if new selected items are not equal to defaultItems
          (aka pre selected items), else return true
        */
        wrapper.vm.$on('is-equal', bool => {
            counter++
            if (counter === 1) expect(bool).to.equal(false)
            if (counter === 2) expect(bool).to.equal(true)
        })
        // mockup onchange event when selecting item
        const vSelect = wrapper.findComponent({ name: 'v-select' })
        // add new item, is-equal should return false
        await vSelect.vm.selectItem({
            id: 'RCR-Writer',
            type: 'services',
        })
        /*
            unselect selected item, is-equal should return true
            as current selected items are equal with defaultItems
        */
        await vSelect.vm.selectItem({
            id: 'RCR-Writer',
            type: 'services',
        })
    })
})
