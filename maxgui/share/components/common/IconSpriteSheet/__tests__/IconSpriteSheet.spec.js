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
import IconSpriteSheet from '@share/components/common/IconSpriteSheet'
import { lodash } from '@share/utils/helpers'
import { APP_CONFIG } from '@rootSrc/utils/constants'

const sheets = APP_CONFIG.ICON_SHEETS
/**
 * This function mockups defining slot content of the component by returning
 * mount function.
 * @param {Object} propsData props passing to IconSpriteSheet component
 * @param {Boolean} isShallow shallow mount the component
 * @returns mount function
 */
function mockupSlotDefining(opts) {
    return mount(
        lodash.merge(
            {
                shallow: true,
                component: IconSpriteSheet,
                slots: { default: 'servers' },
            },
            opts
        )
    )
}

describe('IconSpriteSheet.vue', () => {
    let wrapper

    afterEach(() => {
        wrapper.destroy()
    })

    it(`Should render fallback mdi-bug icon if there is a missing frame`, () => {
        const indexOfFrame = sheets.servers.frames.length
        wrapper = mockupSlotDefining({ propsData: { frame: indexOfFrame } })
        expect(wrapper.vm.icon).to.be.equals('mdi-bug')
    })

    it(`Should ignore frame mxs-color-helper class if color: props is defined`, () => {
        const indexOfFrame = sheets.servers.frames.length - 1
        wrapper = mockupSlotDefining({ propsData: { frame: indexOfFrame, color: 'blue' } })
        expect(wrapper.vm.iconClass).to.be.equals('')
    })

    it(`Should assign accurate color: to v-icon component when color: props is defined`, () => {
        wrapper = mockupSlotDefining({
            shallow: false,
            propsData: { frame: sheets.servers.frames.length - 1, color: 'blue' },
        })
        expect(wrapper.find('.blue--text').exists()).to.be.true
    })

    it(`Should pass accurately props to v-icon`, () => {
        const indexOfFrame = sheets.servers.frames.length - 1
        const passedProps = {
            frame: indexOfFrame,
            color: 'red',
            size: 13,
        }
        wrapper = mockupSlotDefining({ shallow: false, propsData: passedProps })
        wrapper.vm.$nextTick(() => {
            const vIcon = wrapper.findComponent({ name: 'v-icon' })
            expect(vIcon.vm.$props.class).to.be.undefined
            expect(wrapper.vm.iconClass).to.be.equals('') // as mxs-color-helper props is defined
            expect(vIcon.vm.$props.size).to.be.deep.equals(passedProps.size)
            expect(vIcon.vm.$props.color).to.be.deep.equals(passedProps.color)
        })
    })
})

Object.keys(sheets).forEach(sheetName => {
    describe(`IconSpriteSheet.vue - ${sheetName} icons`, () => {
        let wrapper

        afterEach(() => {
            wrapper.destroy()
        })

        it(`Should choose accurately frame and mxs-color-helper classes when
        'default' slot is defined`, () => {
            wrapper = mockupSlotDefining({ slots: { default: sheetName } })
            expect(wrapper.vm.sheet).to.be.deep.equals(sheets[sheetName])
        })

        const iconAndColorTests = {
            icon: `Should render accurately ${sheetName} icon`,
            iconClass: `Should assign accurately class to icon`,
        }
        Object.keys(iconAndColorTests).forEach(key => {
            it(iconAndColorTests[key], async () => {
                const sheet = sheets[sheetName]
                // frame could be either string or number. For `logPriorities`, it is string
                const frames =
                    sheetName === 'logPriorities' ? Object.keys(sheet.frames) : sheet.frames

                wrapper = mockupSlotDefining({
                    propsData: { frame: '' },
                    slots: { default: sheetName },
                })
                for (const [i, f] of frames.entries()) {
                    const frame = sheetName === 'logPriorities' ? f : i
                    await wrapper.setProps({ frame })
                    const expectResult =
                        key === 'icon'
                            ? Object.values(sheet.frames)[i]
                            : `mxs-color-helper ${Object.values(sheet.colorClasses)[i]}`
                    expect(wrapper.vm[key]).to.be.equals(expectResult)
                }
            })
        })
    })
})
