/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import IndexList from '@share/components/common/MxsDdlEditor/IndexList'
import { editorDataStub } from '@share/components/common/MxsDdlEditor/__tests__/stubData'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: IndexList,
                propsData: {
                    value: editorDataStub.defs.key_category_map,
                    dim: { width: 1680, height: 1200 },
                    selectedItems: [],
                },
            },
            opts
        )
    )

describe('index-list', () => {
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
                singleSelect,
                noDataText,
                selectedItems,
            } = wrapper.findComponent({
                name: 'mxs-virtual-scroll-tbl',
            }).vm.$props
            expect(headers).to.be.eql(wrapper.vm.headers)
            expect(data).to.be.eql(wrapper.vm.$data.keyItems)
            expect(itemHeight).to.be.eql(32)
            expect(maxHeight).to.be.eql(wrapper.vm.dim.height)
            expect(boundingWidth).to.be.eql(wrapper.vm.dim.width)
            expect(showSelect).to.be.true
            expect(singleSelect).to.be.true
            expect(noDataText).to.be.eql(
                wrapper.vm.$mxs_t('noEntity', { entityName: wrapper.vm.$mxs_t('indexes') })
            )
            expect(selectedItems).to.be.eql(wrapper.vm.selectedRows)
        })

        it('Should pass accurate data to lazy-select', () => {
            const {
                $attrs: { value, height, items, disabled, name },
                $props: { selectionText },
            } = wrapper.findComponent({
                name: 'lazy-select',
            }).vm
            expect(value).to.be.a('string')
            expect(name).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(items).to.be.eql(wrapper.vm.categories)
            expect(disabled).to.be.a('boolean')
            expect(selectionText).to.be.a('string')
        })

        it('Should pass accurate data to lazy-text-field', () => {
            const { value, height, required, disabled, name } = wrapper.findComponent({
                name: 'lazy-text-field',
            }).vm.$attrs
            expect(value).to.be.a('string')
            expect(name).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(required).to.be.a('boolean')
            expect(disabled).to.be.a('boolean')
        })
    })

    describe(`Computed properties`, () => {
        beforeEach(() => (wrapper = mountFactory()))
        it(`Should return accurate number of headers`, () => {
            expect(wrapper.vm.headers.length).to.be.eql(4)
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
            const spy = sinon.spy(IndexList.methods, 'init')
            wrapper = mountFactory()
            spy.should.have.been.calledOnce
        })
    })
})
