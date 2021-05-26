/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
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
 * @param {Object} props props passing to IconSpriteSheet component
 * @param {Boolean} isShallow shallow mount the component
 * @returns mount function
 */
function mockupSlotDefining(props, isShallow = true) {
    let mountOption = {
        shallow: isShallow,
        component: IconSpriteSheet,
        slots: {
            default: 'status', // currently, maxgui only needs 'status' frame
        },
    }
    if (props) {
        mountOption.props = props
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

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: IconSpriteSheet,
            props: {
                frame: 0,
                size: 13,
                color: undefined, // defining this to override color class of frames
                frames: undefined, // defining valid frame icons if using other icons
                colorClasses: undefined, // defining frame colors if frames is defined
            },
        })
    })

    it(`Should choose accurately frame and color classes when
      'default' slot is defined`, async () => {
        wrapper = mockupSlotDefining()
        expect(wrapper.vm.sheet).to.be.deep.equals(hardCodingStatusSheet)
    })

    it(`Should render accurately status icon`, async () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length - 1
        wrapper = mockupSlotDefining({ frame: indexOfFrame })
        expect(wrapper.vm.icon).to.be.equals(hardCodingStatusSheet.frames[indexOfFrame])
    })

    it(`Should render fallback bug_report icon if there is a missing frame`, async () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length
        wrapper = mockupSlotDefining({ frame: indexOfFrame })
        expect(wrapper.vm.icon).to.be.equals('bug_report')
    })

    it(`Should assign accurately class to icon`, async () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length - 1
        wrapper = mockupSlotDefining({ frame: indexOfFrame })
        expect(wrapper.vm.iconClass).to.be.equals(
            `color ${hardCodingStatusSheet.colorClasses[indexOfFrame]}`
        )
    })

    it(`Should ignore frame color class if color props is defined`, async () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length - 1
        wrapper = mockupSlotDefining({ frame: indexOfFrame, color: 'blue' })
        expect(wrapper.vm.iconClass).to.be.equals('')
    })

    it(`Should assign accurate color to v-icon component when color props is defined`, async () => {
        const indexOfFrame = hardCodingStatusSheet.frames.length - 1
        const isShallow = false
        wrapper = mockupSlotDefining({ frame: indexOfFrame, color: 'blue' }, isShallow)
        expect(wrapper.find('.blue--text').exists()).to.be.true
    })

    it(`Should pass accurately props to v-icon`, async () => {
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
