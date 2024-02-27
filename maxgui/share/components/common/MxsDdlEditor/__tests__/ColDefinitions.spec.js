/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ColDefinitions from '@share/components/common/MxsDdlEditor/ColDefinitions'
import {
    editorDataStub,
    charsetCollationMapStub,
    generatedTypeItemsStub,
    colKeyCategoryMapStub,
} from '@share/components/common/MxsDdlEditor/__tests__/stubData'
import { COL_ATTRS } from '@wsSrc/constants'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: ColDefinitions,
                propsData: {
                    value: editorDataStub.defs,
                    initialData: lodash.cloneDeep(editorDataStub.defs),
                    dim: { width: 1680, height: 1200 },
                    defTblCharset: 'utf8mb4',
                    defTblCollation: 'utf8mb4_general_ci',
                    charsetCollationMap: charsetCollationMapStub,
                    colKeyCategoryMap: colKeyCategoryMapStub,
                },
            },
            opts
        )
    )

describe('col-definitions', () => {
    let wrapper

    describe(`Child component's data communication tests`, () => {
        beforeEach(() => {
            wrapper = mountFactory()
        })
        it(`Should pass accurate data to tbl-toolbar`, () => {
            const { selectedItems, isVertTable } = wrapper.findComponent({
                name: 'tbl-toolbar',
            }).vm.$props
            expect(selectedItems).to.be.eql(wrapper.vm.$data.selectedItems)
            expect(isVertTable).to.be.eql(wrapper.vm.$data.isVertTable)
        })

        it('Should pass accurate data to mxs-filter-list', () => {
            const { value, items } = wrapper.findComponent({
                name: 'mxs-filter-list',
            }).vm.$props
            expect(value).to.be.eql(wrapper.vm.$data.hiddenColSpecs)
            expect(items).to.be.eql(wrapper.vm.colSpecs)
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
                isVertTable,
                selectedItems,
            } = wrapper.findComponent({
                name: 'mxs-virtual-scroll-tbl',
            }).vm.$props
            expect(headers).to.be.eql(wrapper.vm.headers)
            expect(data).to.be.eql(wrapper.vm.rows)
            expect(itemHeight).to.be.eql(32)
            expect(maxHeight).to.be.eql(wrapper.vm.tableMaxHeight)
            expect(boundingWidth).to.be.eql(wrapper.vm.dim.width)
            expect(showSelect).to.be.true
            expect(selectedItems).to.be.eql(wrapper.vm.$data.selectedItems)
            expect(isVertTable).to.be.eql(wrapper.vm.$data.isVertTable)
        })

        it('Should pass accurate data to data-type-input', () => {
            const { value, height, items } = wrapper.findComponent({
                name: 'data-type-input',
            }).vm.$props
            expect(value).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(items).to.be.eql(wrapper.vm.dataTypes)
        })

        it('Should pass accurate data to lazy-select', () => {
            const { value, height, name, items, disabled } = wrapper.findComponent({
                name: 'lazy-select',
            }).vm.$attrs
            expect(value).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(name).to.be.eql(COL_ATTRS.GENERATED)
            expect(items).to.be.eql(generatedTypeItemsStub)
            expect(disabled).to.be.a('boolean')
        })

        it('Should pass accurate data to lazy-text-field', () => {
            const { value, height, name, required } = wrapper.findComponent({
                name: 'lazy-text-field',
            }).vm.$attrs
            expect(value).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(name).to.be.a('string')
            expect(required).to.be.a('boolean')
        })

        it('Should pass accurate data to bool-input', () => {
            const { value, height, field, rowData } = wrapper.findComponent({
                name: 'bool-input',
            }).vm.$props
            expect(value).to.be.a('boolean')
            expect(height).to.be.eql(28)
            expect(field).to.be.a('string')
            expect(rowData).to.be.an('array')
        })

        it('Should pass accurate data to charset-collate-input', async () => {
            await wrapper.setData({ hiddenColSpecs: [] })
            const {
                value,
                height,
                field,
                rowData,
                charsetCollationMap,
                defTblCharset,
                defTblCollation,
            } = wrapper.findComponent({
                name: 'charset-collate-input',
            }).vm.$props
            expect(value).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(field).to.be.a('string')
            expect(rowData).to.be.an('array')
            expect(charsetCollationMap).to.be.eql(wrapper.vm.$props.charsetCollationMap)
            expect(defTblCharset).to.be.eql(wrapper.vm.$props.defTblCharset)
            expect(defTblCollation).to.be.eql(wrapper.vm.$props.defTblCollation)
        })
    })

    describe(`Computed properties`, () => {
        beforeEach(() => (wrapper = mountFactory()))
        it(`Should return accurate value for defs`, () => {
            expect(wrapper.vm.defs).to.be.eql(wrapper.vm.$props.value)
        })

        it(`Should emit input event`, () => {
            wrapper.vm.defs = null
            expect(wrapper.emitted('input')[0]).to.be.eql([null])
        })

        it(`Should return accurate number of headers`, () => {
            expect(wrapper.vm.headers.length).to.be.eql(14)
        })

        it(`Should return transformedCols with expected fields`, () => {
            expect(wrapper.vm.transformedCols).to.be.an('array').that.is.not.empty
            expect(wrapper.vm.transformedCols[0]).to.have.all.keys(...Object.values(COL_ATTRS))
        })

        it(`rows should be a 2d array`, () => {
            expect(wrapper.vm.rows).to.be.an('array').that.is.not.empty
            expect(wrapper.vm.rows.every(row => Array.isArray(row))).to.be.true
        })
    })

    describe(`Created hook and method tests`, () => {
        afterEach(() => sinon.restore())

        it(`Should call handleShowColSpecs`, () => {
            const spy = sinon.spy(ColDefinitions.methods, 'handleShowColSpecs')
            wrapper = mountFactory()
            spy.should.have.been.calledOnce
        })

        it(`Should handle deleteSelectedRows as expected`, () => {
            wrapper = mountFactory()
            const spy = sinon.spy(wrapper.vm, 'keySideEffect')
            // mock deleting all rows
            wrapper.vm.deleteSelectedRows(wrapper.vm.rows)
            expect(wrapper.emitted('input')[0][0].col_map).to.be.eql({})
            spy.should.have.been.calledOnce
        })

        it(`Should handle addNewCol as expected`, () => {
            wrapper = mountFactory()
            wrapper.vm.addNewCol()
            const oldCols = Object.values(editorDataStub.defs.col_map)
            const newCols = Object.values(wrapper.emitted('input')[0][0].col_map)
            expect(newCols.length).to.be.eql(oldCols.length + 1)
            expect(newCols.at(-1)).to.have.all.keys(
                'ai',
                'charset',
                'collate',
                'comment',
                'data_type',
                'default_exp',
                'generated',
                'id',
                'name',
                'nn',
                'un',
                'zf'
            )
        })
    })
})
