/*
 * Copyright (c) 2023 MariaDB plc
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
import FkDefinitions from '@share/components/common/MxsDdlEditor/FkDefinitions'
import { lodash } from '@share/utils/helpers'
import { CREATE_TBL_TOKENS as tokens, FK_EDITOR_ATTRS } from '@wsSrc/constants'

const mockFkObj = {
    cols: [{ id: 'col_3e0af061-3b54-11ee-a8e8-25db6da41f2a' }],
    id: 'key_3e0b1773-3b54-11ee-a8e8-25db6da41f2a',
    name: 'employees_ibfk_0',
    ref_cols: [{ id: 'col_3e0af062-3b54-11ee-a8e8-25db6da41f2a' }],
    ref_schema_name: 'company',
    ref_tbl_id: 'tbl_750b4681-3b5b-11ee-a3ad-dfd43862371d',
    on_delete: 'NO ACTION',
    on_update: 'NO ACTION',
}
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: FkDefinitions,
                propsData: {
                    value: {
                        [tokens.foreignKey]: { [mockFkObj.id]: mockFkObj },
                    },
                    lookupTables: {},
                    newLookupTables: {},
                    allLookupTables: [],
                    allTableColMap: {},
                    refTargets: [],
                    tablesColNameMap: {},
                    tableId: '',
                    dim: {},
                    connData: {},
                    charsetCollationMap: {},
                },
            },
            opts
        )
    )

let wrapper

describe('fk-definitions', () => {
    describe(`Child component's data communication tests`, () => {
        it(`Should show v-progress-linear when isLoading is true`, async () => {
            wrapper = mountFactory()
            await wrapper.setData({ isLoading: true })
            expect(wrapper.findComponent({ name: 'v-progress-linear' }).exists()).to.be.true
        })

        it(`Should pass accurate data to tbl-toolbar`, () => {
            wrapper = mountFactory()
            const { selectedItems, isVertTable } = wrapper.findComponent({
                name: 'tbl-toolbar',
            }).vm.$props
            expect(selectedItems).to.be.eql(wrapper.vm.$data.selectedItems)
            expect(isVertTable).to.be.eql(wrapper.vm.$data.isVertTable)
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
                noDataText,
                selectedItems,
            } = wrapper.findComponent({
                name: 'mxs-virtual-scroll-tbl',
            }).vm.$props
            expect(headers).to.be.eql(wrapper.vm.headers)
            expect(data).to.be.eql(wrapper.vm.rows)
            expect(itemHeight).to.be.eql(32)
            expect(maxHeight).to.be.eql(wrapper.vm.dim.height - wrapper.vm.$data.headerHeight)
            expect(boundingWidth).to.be.eql(wrapper.vm.dim.width)
            expect(showSelect).to.be.true
            expect(noDataText).to.be.eql(
                wrapper.vm.$mxs_t('noEntity', { entityName: wrapper.vm.$mxs_t('foreignKeys') })
            )
            expect(selectedItems).to.be.eql(wrapper.vm.$data.selectedItems)
            expect(isVertTable).to.be.eql(wrapper.vm.$data.isVertTable)
        })

        it(`Should pass accurate data to lazy-text-field`, () => {
            wrapper = mountFactory()
            const { value, height, name, required } = wrapper.findComponent({
                name: 'lazy-text-field',
            }).vm.$attrs
            expect(value).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(name).to.be.eql(FK_EDITOR_ATTRS.NAME)
            expect(required).to.be.true
        })

        it(`Should pass accurate data to ref-target-input`, () => {
            wrapper = mountFactory()
            const {
                value,
                height,
                name,
                items,
                ['item-text']: itemText,
                ['item-value']: itemValue,
                rules,
                required,
            } = wrapper.find('.ref-target-input').vm.$attrs
            expect(value).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(name).to.be.eql(FK_EDITOR_ATTRS.REF_TARGET)
            expect(items).to.be.eql(wrapper.vm.$props.refTargets)
            expect(itemText).to.equal('text')
            expect(itemValue).to.equal('id')
            expect(rules).to.be.an('array')
            expect(required).to.be.true
        })

        it(`Should pass accurate data to ref-opt-input`, () => {
            wrapper = mountFactory()
            const { value, height, items } = wrapper.find('.ref-opt-input').vm.$attrs
            expect(value).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(items).to.be.eql(wrapper.vm.refOptItems)
        })

        it(`Should pass accurate data to fk-col-field-input`, () => {
            wrapper = mountFactory()
            const {
                value,
                field,
                height,
                referencingColOptions,
                refColOpts,
            } = wrapper.findComponent({
                name: 'fk-col-field-input',
            }).vm.$props
            expect(value).to.be.an('array')
            expect(field).to.be.a('string')
            expect(height).to.be.eql(28)
            expect(referencingColOptions).to.be.eql(wrapper.vm.referencingColOptions)
            expect(refColOpts).to.be.an('array')
        })
    })

    describe(`Computed properties and created hook tests`, () => {
        afterEach(() => sinon.restore())
        it('Should return accurate number of headers', () => {
            wrapper = mountFactory()
            expect(wrapper.vm.headers.length).to.equal(7)
        })

        const twoWayBindingComputedProperties = {
            tmpLookupTables: 'newLookupTables',
            keyCategoryMap: 'value',
        }
        Object.keys(twoWayBindingComputedProperties).forEach(property => {
            let propName = twoWayBindingComputedProperties[property],
                evtName = `update:${propName}`
            it(`Should return accurate value for ${property}`, () => {
                wrapper = mountFactory()
                expect(wrapper.vm[property]).to.be.eql(wrapper.vm.$props[propName])
            })
            if (propName === 'value') evtName = 'input'
            it(`Should emit ${evtName} event`, () => {
                wrapper = mountFactory()
                wrapper.vm[property] = null
                expect(wrapper.emitted(evtName)[0]).to.be.eql([null])
            })
        })

        const computedDataTypeMap = {
            array: ['fks', 'rows', 'unknownTargets', 'referencingColOptions'],
            object: ['plainKeyMap', 'plainKeyNameMap', 'fkMap', 'fkRefTblMap'],
        }
        Object.keys(computedDataTypeMap).forEach(type => {
            computedDataTypeMap[type].forEach(property => {
                it(`${property} should return an ${type}`, () => {
                    wrapper = mountFactory()
                    expect(wrapper.vm[property]).to.be.an(type)
                })
            })
        })

        it(`rows should be an 2d array`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.rows)
                .to.be.an('array')
                .that.satisfies(arr => arr.every(subArray => Array.isArray(subArray)))
        })

        it(`unknownTargets should be an array of objects`, () => {
            wrapper = mountFactory({
                computed: {
                    // mock fks so that it has unknown targets
                    fks: () => [
                        {
                            ...lodash.pickBy(
                                mockFkObj,
                                (_, key) => key !== 'ref_cols' && key !== 'ref_tbl_id'
                            ),
                            ref_cols: [{ name: 'id' }],
                            ref_tbl_name: 'department',
                        },
                    ],
                },
            })
            expect(wrapper.vm.unknownTargets.length).to.be.eql(1)
            const targetObj = wrapper.vm.unknownTargets[0]
            expect(targetObj).to.be.an('object')
            expect(targetObj).to.have.property('schema')
            expect(targetObj).to.have.property('tbl')
        })

        it(`Should call init on created hook`, () => {
            const spy = sinon.spy(FkDefinitions.methods, 'init')
            wrapper = mountFactory()
            spy.should.have.been.calledOnce
        })
    })

    describe('Watchers tests', () => {
        beforeEach(() => {
            wrapper = mountFactory()
        })
        afterEach(() => sinon.restore())

        it('stagingKeyCategoryMaps should not be empty', () => {
            expect(wrapper.vm.stagingKeyCategoryMap).to.not.be.empty
        })

        it('should emit input event when stagingKeyCategoryMap changes', async () => {
            await wrapper.setData({ stagingKeyCategoryMap: {} })
            expect(wrapper.emitted('input')[0]).to.deep.equal([{}])
        })

        it(`Should call assignData when keyCategoryMap is changed outside
          the component`, async () => {
            const spy = sinon.spy(wrapper.vm, 'assignData')
            await wrapper.setProps({ value: {} })
            spy.should.have.been.calledOnce
        })

        it(`Should not call assignData`, () => {
            const spy = sinon.spy(wrapper.vm, 'assignData')
            spy.should.not.been.called
        })
    })
})
