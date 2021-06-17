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
import OutlinedOverviewCard from '@/components/common/OutlinedOverviewCard'

describe('OutlinedOverviewCard.vue', () => {
    let wrapper

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: OutlinedOverviewCard,
        })
    })

    it(`Should remove border-radius - i.e tile props is true by default`, async () => {
        expect(wrapper.find('.rounded-0').exists()).to.be.equal(true)
    })

    it(`Should have border-radius - i.e tile props is false`, async () => {
        await wrapper.setProps({
            tile: false,
        })
        expect(wrapper.find('.rounded-0').exists()).to.be.equal(false)
    })

    it(`Should add wrapperClass`, async () => {
        await wrapper.setProps({
            wrapperClass: 'test-class',
        })
        expect(wrapper.find('.test-class').exists()).to.be.equal(true)
    })

    it(`Should add cardClass`, async () => {
        await wrapper.setProps({
            cardClass: 'test-card-class',
        })
        expect(wrapper.find('.test-card-class').exists()).to.be.equal(true)
    })

    it(`Should emit card-hover when card is hovered and hoverableCard props is true`, async () => {
        await wrapper.setProps({
            hoverableCard: true,
            cardClass: 'hoverable-card',
        })
        let count = 0
        wrapper.vm.$on('card-hover', isHover => {
            count++
            count === 1 && expect(isHover).to.be.equal(true)
            count === 2 && expect(isHover).to.be.equal(false)
        })

        let cardDiv = wrapper.find('.hoverable-card')
        await cardDiv.trigger('mouseenter')
        await cardDiv.trigger('mouseleave')
        expect(count).to.be.equal(2)
    })
})
