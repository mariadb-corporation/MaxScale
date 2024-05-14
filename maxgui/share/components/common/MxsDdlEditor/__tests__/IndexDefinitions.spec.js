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
import IndexDefinitions from '@share/components/common/MxsDdlEditor/IndexDefinitions'
import {
    editorDataStub,
    tableColNameMapStub,
    tableColMapStub,
} from '@share/components/common/MxsDdlEditor/__tests__/stubData'
import { CREATE_TBL_TOKENS as tokens } from '@wsSrc/constants'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: IndexDefinitions,
                propsData: {
                    value: editorDataStub.defs.key_category_map,
                    dim: { width: 1680, height: 1200 },
                    tableColNameMap: tableColNameMapStub,
                    tableColMap: tableColMapStub,
                },
            },
            opts
        )
    )
function getKeysByCategory({ wrapper, category }) {
    return Object.values(wrapper.vm.stagingKeyCategoryMap[category] || {})
}
describe('index-definitions', () => {
    let wrapper

    describe(`Child component's data communication tests`, () => {
        beforeEach(async () => {
            wrapper = mountFactory()
            await wrapper.vm.$nextTick()
        })
        it(`Should pass accurate data to tbl-toolbar`, () => {
            const { selectedItems, showRotateTable, reverse } = wrapper.findComponent({
                name: 'tbl-toolbar',
            }).vm.$props
            expect(selectedItems).to.be.eql(wrapper.vm.$data.selectedItems)
            expect(showRotateTable).to.be.false
            expect(reverse).to.be.true
        })

        it('Should pass accurate data to index-list', () => {
            const { value, selectedItems } = wrapper.findComponent({
                name: 'index-list',
            }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.stagingKeyCategoryMap)
            expect(selectedItems).to.be.eql(wrapper.vm.$data.selectedItems)
        })

        it(`Should pass accurate data to index-col-list`, () => {
            const { value, keyId, category, tableColNameMap, tableColMap } = wrapper.findComponent({
                name: 'index-col-list',
            }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.stagingKeyCategoryMap)
            expect(keyId).to.be.eql(wrapper.vm.selectedKeyId)
            expect(category).to.be.eql(wrapper.vm.selectedKeyCategory)
            expect(tableColNameMap).to.be.eql(tableColNameMapStub)
            expect(tableColMap).to.be.eql(tableColMapStub)
        })

        it(`Should not render index-col-list when there is no selectedKeyId`, () => {
            wrapper = mountFactory({ computed: { selectedKeyId: () => null } })
            expect(wrapper.findComponent({ name: 'index-col-list' }).exists()).to.be.false
        })
    })

    describe(`Computed properties`, () => {
        beforeEach(() => (wrapper = mountFactory()))
        it(`Should return accurate value for keyCategoryMap`, () => {
            expect(wrapper.vm.keyCategoryMap).to.be.eql(wrapper.vm.$props.value)
        })
        it(`Should emit input event`, () => {
            wrapper.vm.keyCategoryMap = null
            expect(wrapper.emitted('input')[0]).to.be.eql([null])
        })

        it(`selectedItem should be an array`, () => {
            expect(wrapper.vm.selectedItem).to.be.an('array')
        })
    })

    describe(`Created hook and method tests`, () => {
        afterEach(() => sinon.restore())

        it(`Should call init`, () => {
            const spy = sinon.spy(IndexDefinitions.methods, 'init')
            wrapper = mountFactory()
            spy.should.have.been.calledOnce
        })

        it(`Should handle deleteSelectedKeys as expected`, () => {
            wrapper = mountFactory()
            const spy = sinon.spy(wrapper.vm, 'selectFirstItem')
            const { selectedKeyId, selectedKeyCategory } = wrapper.vm

            // mock deleting the selected key
            wrapper.vm.deleteSelectedKeys()
            const stagingKeyMap = wrapper.vm.stagingKeyCategoryMap[selectedKeyCategory] || {}
            expect(stagingKeyMap).to.not.have.property(selectedKeyId)
            spy.should.have.been.calledOnce
            expect(wrapper.vm.selectedKeyId).to.not.equal(selectedKeyId)
        })

        it(`Should handle addNewKey as expected`, () => {
            wrapper = mountFactory()
            const oldPlainKeys = getKeysByCategory({ wrapper, category: tokens.key })
            wrapper.vm.addNewKey()
            const newPlainKeys = getKeysByCategory({ wrapper, category: tokens.key })
            expect(newPlainKeys.length).to.be.eql(oldPlainKeys.length + 1)
            expect(newPlainKeys.at(-1)).to.have.all.keys('id', 'cols', 'name')
        })
    })
})
