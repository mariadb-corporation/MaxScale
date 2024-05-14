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
import MxsSplitPane from '@share/components/common/MxsSplitPane'

const defaultValue = 50
describe('mxs-split-pane', () => {
    let wrapper
    before(() => {
        wrapper = mount({
            component: MxsSplitPane,
            propsData: { value: defaultValue, boundary: 1000 },
            split: 'vert',
        })
    })
    it(`Should pass accurate data to split-pane left`, () => {
        const { isLeft, split } = wrapper.find('[data-test="pane-left"]').vm.$props
        expect(isLeft).to.be.true
        expect(split).to.be.eql(wrapper.vm.$props.split)
    })
    it(`Should pass accurate data to split-pane right`, () => {
        const { isLeft, split } = wrapper.find('[data-test="pane-right"]').vm.$props
        expect(isLeft).to.be.false
        expect(split).to.be.eql(wrapper.vm.$props.split)
    })
    it(`Should pass accurate data to resize-handle`, () => {
        const { active, split } = wrapper.findComponent({ name: 'resize-handle' }).vm.$props
        expect(active).to.be.eql(wrapper.vm.$data.active)
        expect(split).to.be.eql(wrapper.vm.$props.split)
    })
    it(`Should update currPct when value props is changed in the parent component`, async () => {
        expect(wrapper.vm.$data.currPct).to.equal(defaultValue)
        await wrapper.setProps({ value: 100 })
        expect(wrapper.vm.$data.currPct).to.equal(100)
    })
})
