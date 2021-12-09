/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import IconSpriteSheet from '@/components/common/IconSpriteSheet'

/**
 * This function mockups defining slot content of the component by returning
 * mount function.
 * @param {Object} propsData props passing to IconSpriteSheet component
 * @param {Boolean} isShallow shallow mount the component
 * @returns mount function
 */
function mockupSlotDefining(propsData, isShallow = true) {
    let mountOption = {
        propsData,
        shallow: isShallow,
        component: IconSpriteSheet,
        slots: {
            default: 'status', // currently, maxgui only needs 'status' frame
        },
    }
    return mount(mountOption)
}

const hardCodingStatusSheet = {
    frames: [
        '$vuetify.icons.statusError',
        '$vuetify.icons.statusOk',
        '$vuetify.icons.statusWarning',
        '$vuetify.icons.statusInfo',
    ],
    colorClasses: ['text-error', 'text-success', 'text-warning', 'text-info'],
}

describe('IconSpriteSheet.vue', () => {
    let wrapper

    afterEach(() => {
        wrapper.destroy()
    })

    it(`Should choose accurately frame and color classes when
      'default' slot is defined`, () => {
        wrapper = mockupSlotDefining()
        expect(wrapper.vm.sheet).to.be.deep.equals(hardCodingStatusSheet)
    })

    it(`Should render accurately status icon`, () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length - 1
        wrapper = mockupSlotDefining({ frame: indexOfFrame })
        expect(wrapper.vm.icon).to.be.equals(hardCodingStatusSheet.frames[indexOfFrame])
    })

    it(`Should render fallback bug_report icon if there is a missing frame`, () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length
        wrapper = mockupSlotDefining({ frame: indexOfFrame })
        expect(wrapper.vm.icon).to.be.equals('bug_report')
    })

    it(`Should assign accurately class to icon`, () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length - 1
        wrapper = mockupSlotDefining({ frame: indexOfFrame })
        expect(wrapper.vm.iconClass).to.be.equals(
            `color ${hardCodingStatusSheet.colorClasses[indexOfFrame]}`
        )
    })

    it(`Should ignore frame color class if color props is defined`, () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length - 1
        wrapper = mockupSlotDefining({ frame: indexOfFrame, color: 'blue' })
        expect(wrapper.vm.iconClass).to.be.equals('')
    })

    it(`Should assign accurate color to v-icon component when color props is defined`, () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length - 1
        const isShallow = false
        wrapper = mockupSlotDefining({ frame: indexOfFrame, color: 'blue' }, isShallow)
        expect(wrapper.find('.blue--text').exists()).to.be.true
    })

    it(`Should pass accurately props to v-icon`, () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length - 1
        const isShallow = false
        const passedProps = {
            frame: indexOfFrame,
            color: 'red',
            size: 13,
        }
        wrapper = mockupSlotDefining(passedProps, isShallow)
        wrapper.vm.$nextTick(() => {
            const vIcon = wrapper.findComponent({ name: 'v-icon' })
            expect(vIcon.vm.$props.class).to.be.undefined
            expect(wrapper.vm.iconClass).to.be.equals('') // as color props is defined
            expect(vIcon.vm.$props.size).to.be.deep.equals(passedProps.size)
            expect(vIcon.vm.$props.color).to.be.deep.equals(passedProps.color)
        })
    })
})
