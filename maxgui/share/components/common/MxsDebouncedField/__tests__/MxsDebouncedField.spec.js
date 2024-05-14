/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import MxsDebouncedField from '@share/components/common/MxsDebouncedField'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts => mount(lodash.merge({ component: MxsDebouncedField }, opts))

describe('mxs-debounced-field', () => {
    let wrapper
    afterEach(() => sinon.restore())

    it(`Should pass accurate data to v-text-field`, () => {
        wrapper = mountFactory()
        const { value } = wrapper.findComponent({ name: 'v-text-field' }).vm.$props
        expect(value).to.be.eql(wrapper.vm.$data.inputValue)
    })

    it('Should set the inputValue based on $attrs.value', () => {
        wrapper = mountFactory({ attrs: { value: 'initialValue' } })
        expect(wrapper.vm.$data.inputValue).to.equal('initialValue')
    })

    it('Should filters out "input" listeners', () => {
        const listenersStub = { input: () => {}, click: () => {} }
        wrapper = mountFactory({ listeners: listenersStub })
        expect(wrapper.vm.filteredListeners).to.include.keys('click')
    })

    it('Should emit input event after debounceTime', async () => {
        const debounceTime = 100
        const clock = sinon.useFakeTimers()
        const wrapper = mountFactory({ propsData: { debounceTime } })
        await wrapper.setData({ inputValue: 'testValue' })
        wrapper.vm.handleInputDebounced('testValue')
        const threshold = 50
        // set for a time slightly smaller than debounceTime
        clock.tick(debounceTime - threshold)
        expect(wrapper.emitted().input).to.be.undefined
        clock.tick(threshold)
        expect(wrapper.emitted().input[0][0]).to.equal('testValue')
    })
})
