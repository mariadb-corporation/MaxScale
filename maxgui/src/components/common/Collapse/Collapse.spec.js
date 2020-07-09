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
import Collapse from '@/components/common/Collapse'

describe('Collapse.vue', () => {
    let wrapper
    // mockup parent value passing to Collapse

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: Collapse,
            props: {
                wrapperClass: 'collapse-wrapper',
                isContentVisible: true,
                title: 'Collapse title',
                toggleOnClick: () => {
                    // mockup isContentVisible reactivity props
                    wrapper.setProps({ isContentVisible: !wrapper.props().isContentVisible })
                },
            },
        })
    })

    it('Component collapses when toggle arrow is clicked', () => {
        // this calls toggleOnClick cb which is handled in parent component
        wrapper.find('.arrow-toggle').trigger('click')
        // component is collapsed when isContentVisible === false
        expect(wrapper.props().isContentVisible).to.be.false
    })

    it('Display edit button when hover', async () => {
        // edit button is rendered only when onEdit props is passed with a function
        await wrapper.setProps({
            onEdit: () => wrapper.vm.$store.Vue.Logger('Collapse').info('onEdit cb'),
        })
        wrapper.find('.collapse-wrapper').trigger('mouseenter')
        expect(wrapper.vm.$data.showEditBtn).to.be.true
    })

    it(`Display "add" button if onAddClick props is passed, onAddClick callback should be
      triggered`, async () => {
        let eventFired = 0
        // edit button is rendered only when onEdit props is passed with a function
        await wrapper.setProps({
            onAddClick: () => {
                eventFired++
            },
        })
        wrapper.find('.add-btn').trigger('click')
        expect(eventFired).to.equal(1)
    })

    it(`Display "Done Editing" button if isEditing props is true,
      after doneEditingCb is called, value of isEditing props should be false`, async () => {
        let eventFired = 0
        // edit button is rendered only when onEdit props is passed with a function
        await wrapper.setProps({
            isEditing: true,
            doneEditingCb: () => {
                eventFired++

                wrapper.setProps({ isEditing: false })
            },
        })
        wrapper.find('.don-editing-btn').trigger('click')
        expect(eventFired).to.equal(1)
        expect(wrapper.props().isEditing).to.be.false
    })
})
