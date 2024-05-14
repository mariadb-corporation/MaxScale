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
import IndexColList from '@share/components/common/MxsDdlEditor/IndexColList'
import {
    editorDataStub,
    tableColNameMapStub,
    tableColMapStub,
} from '@share/components/common/MxsDdlEditor/__tests__/stubData'
import { CREATE_TBL_TOKENS as tokens, KEY_COL_EDITOR_ATTRS } from '@wsSrc/constants'
import { lodash } from '@share/utils/helpers'

const stubKeyId = Object.keys(editorDataStub.defs.key_category_map[tokens.primaryKey])[0]

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: IndexColList,
                propsData: {
                    value: editorDataStub.defs.key_category_map,
                    dim: { width: 1680, height: 1200 },
                    keyId: stubKeyId,
                    category: tokens.primaryKey,
                    tableColNameMap: tableColNameMapStub,
                    tableColMap: tableColMapStub,
                },
            },
            opts
        )
    )

describe('index-col-list', () => {
    let wrapper

    describe(`Child component's data communication tests`, () => {
        beforeEach(async () => {
            wrapper = mountFactory()
            await wrapper.vm.$nextTick()
        })
        it(`Should pass accurate data to mxs-virtual-scroll-tbl`, () => {
            wrapper = mountFactory()
            const {
                headers,
                data,
                itemHeight,
                maxHeight,
                boundingWidth,
                showSelect,
                selectedItems,
                showRowCount,
            } = wrapper.findComponent({
                name: 'mxs-virtual-scroll-tbl',
            }).vm.$props
            expect(headers).to.be.eql(wrapper.vm.headers)
            expect(data).to.be.eql(wrapper.vm.$data.rows)
            expect(itemHeight).to.be.eql(32)
            expect(maxHeight).to.be.eql(wrapper.vm.dim.height)
            expect(boundingWidth).to.be.eql(wrapper.vm.dim.width)
            expect(showSelect).to.be.true
            expect(showRowCount).to.be.false
            expect(selectedItems).to.be.eql(wrapper.vm.$data.selectedItems)
        })

        it('Should pass accurate data to lazy-select', () => {
            const { value, height, items, name } = wrapper.findComponent({
                name: 'lazy-select',
            }).vm.$attrs
            expect(value).to.be.a('string')
            expect(name).to.be.eql(KEY_COL_EDITOR_ATTRS.ORDER_BY)
            expect(height).to.be.eql(28)
            expect(items).to.be.eql(wrapper.vm.orderByItems)
        })

        it('Should pass accurate data to lazy-text-field', () => {
            const { value, height, name } = wrapper.findComponent({
                name: 'lazy-text-field',
            }).vm.$attrs
            expect(value).to.be.oneOf([Number, undefined])
            expect(name).to.be.eql(KEY_COL_EDITOR_ATTRS.LENGTH)
            expect(height).to.be.eql(28)
        })
    })

    describe(`Computed properties`, () => {
        beforeEach(() => (wrapper = mountFactory()))
        it(`Should return accurate number of headers`, () => {
            expect(wrapper.vm.headers.length).to.be.eql(6)
        })

        it(`Should return accurate value for keyCategoryMap`, () => {
            expect(wrapper.vm.keyCategoryMap).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit input event`, () => {
            wrapper.vm.keyCategoryMap = null
            expect(wrapper.emitted('input')[0]).to.be.eql([null])
        })
    })

    describe(`Created hook and method tests`, () => {
        afterEach(() => sinon.restore())

        it(`Should call init`, () => {
            const spy = sinon.spy(IndexColList.methods, 'init')
            wrapper = mountFactory()
            spy.should.have.been.calledOnce
        })
    })
})
