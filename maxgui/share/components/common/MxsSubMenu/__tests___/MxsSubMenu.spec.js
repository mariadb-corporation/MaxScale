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
import MxsSubMenu from '@share/components/common/MxsSubMenu'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: MxsSubMenu,
                propsData: { items: [] },
                // mock menu opens
                data: () => ({ menuOpen: true }),
            },
            opts
        )
    )

describe('mxs-sub-menu', () => {
    let wrapper
    it('Should emit item-click event on clicking child menu item', async () => {
        const items = [{ text: 'Item 1' }, { text: 'Item 2' }]
        wrapper = mountFactory({ propsData: { items } })
        const menuItems = wrapper.findAll('[data-test="child-menu-item"]')
        // Click the first menu item
        await menuItems.at(0).trigger('click')
        expect(wrapper.emitted('item-click')[0]).to.deep.equal([{ text: 'Item 1' }])
    })

    it('Should pass accurate data to mxs-sub-menu', () => {
        const submenuItems = [{ text: 'Subitem 1' }, { text: 'Subitem 2' }]
        const submenuPropsStub = {
            isSubMenu: true,
            text: 'Parent Item',
            nestedMenuTransition: 'scale-transition',
            nestedMenuOpenDelay: 150,
        }
        wrapper = mountFactory({
            propsData: { items: [{ text: 'Parent Item', children: submenuItems }] },
        })
        // Click the parent menu item to open the submenu
        const parentMenuItem = wrapper.findAllComponents({ name: 'mxs-sub-menu' }).at(1)
        expect(parentMenuItem.exists()).to.be.true
        const {
            $props: { items, submenuProps },
            $attrs: {
                ['offset-y']: offsetY,
                transition,
                ['open-delay']: openDelay,
                ['open-on-hover']: openOnHover,
            },
        } = parentMenuItem.vm
        expect(items).to.be.eql(submenuItems)
        expect(submenuProps).to.be.eql(submenuPropsStub)
        expect(parentMenuItem.vm.$attrs).to.include.keys('offset-x')
        expect(offsetY).to.be.false
        expect(transition).to.equal(submenuPropsStub.nestedMenuTransition)
        expect(openDelay).to.equal(submenuPropsStub.nestedMenuOpenDelay)
        expect(openOnHover).to.be.true
    })

    it(`Should return accurate value for isOpened`, () => {
        wrapper = mountFactory()
        expect(wrapper.vm.isOpened).to.be.eql(wrapper.vm.$attrs.value)
    })

    it(`Should emit input event`, () => {
        wrapper = mountFactory({ attrs: { value: true } })
        wrapper.vm.isOpened = false
        expect(wrapper.emitted('input')[0]).to.be.eql([false])
    })
})
