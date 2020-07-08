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
})
