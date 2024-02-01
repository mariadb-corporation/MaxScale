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
import MxsDdlEditor from '@share/components/common/MxsDdlEditor'
import { lodash } from '@share/utils/helpers'
import { DDL_EDITOR_SPECS } from '@wsSrc/constants'
import {
    editorDataStub,
    charsetCollationMapStub,
} from '@share/components/common/MxsDdlEditor/__tests__/stubData'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: MxsDdlEditor,
                propsData: {
                    value: editorDataStub,
                    dim: { width: 500, height: 800 },
                    initialData: lodash.cloneDeep(editorDataStub),
                    connData: {},
                    activeSpec: '',
                    lookupTables: { [editorDataStub.id]: editorDataStub },
                },
                computed: {
                    charset_collation_map: () => charsetCollationMapStub,
                    def_db_charset_map: () => ({ test: 'utf8mb4' }),
                },
            },
            opts
        )
    )

let wrapper

describe('mxs-ddl-editor', () => {
    afterEach(() => sinon.restore())

    describe(`Child component's data communication tests`, () => {
        it(`Should pass accurate data to table-opts`, () => {
            wrapper = mountFactory()
            const {
                value,
                engines,
                charsetCollationMap,
                defDbCharset,
                isCreating,
                schemas,
            } = wrapper.findComponent({
                name: 'table-opts',
            }).vm.$props
            expect(value).to.be.eql(wrapper.vm.tblOpts)
            expect(engines).to.be.eql(wrapper.vm.engines)
            expect(charsetCollationMap).to.be.eql(wrapper.vm.charset_collation_map)
            expect(defDbCharset).to.be.eql('utf8mb4')
            expect(isCreating).to.be.eql(wrapper.vm.$props.isCreating)
            expect(schemas).to.be.eql(wrapper.vm.schemas)
        })

        it(`Should pass accurate data to col-definitions`, () => {
            wrapper = mountFactory({ computed: { activeSpecTab: () => DDL_EDITOR_SPECS.COLUMNS } })
            const {
                value,
                charsetCollationMap,
                initialData,
                dim,
                defTblCharset,
                defTblCollation,
                colKeyCategoryMap,
            } = wrapper.findComponent({
                name: 'col-definitions',
            }).vm.$props
            expect(value).to.be.eql(wrapper.vm.defs)
            expect(charsetCollationMap).to.be.eql(wrapper.vm.charset_collation_map)
            expect(initialData).to.be.eql(wrapper.vm.initialDefinitions)
            expect(dim).to.be.eql(wrapper.vm.tabDim)
            expect(defTblCharset).to.be.eql(wrapper.vm.tblOpts.charset)
            expect(defTblCollation).to.be.eql(wrapper.vm.tblOpts.collation)
            expect(colKeyCategoryMap).to.be.eql(wrapper.vm.colKeyCategoryMap)
        })

        it(`Should pass accurate data to fk-definitions`, () => {
            wrapper = mountFactory({ computed: { activeSpecTab: () => DDL_EDITOR_SPECS.FK } })
            const {
                value,
                lookupTables,
                newLookupTables,
                allLookupTables,
                allTableColMap,
                refTargets,
                tablesColNameMap,
                tableId,
                dim,
                connData,
                charsetCollationMap,
            } = wrapper.findComponent({
                name: 'fk-definitions',
            }).vm.$props
            expect(value).to.be.eql(wrapper.vm.keyCategoryMap)
            expect(lookupTables).to.be.eql(wrapper.vm.$props.lookupTables)
            expect(newLookupTables).to.be.eql(wrapper.vm.$data.newLookupTables)
            expect(allLookupTables).to.be.eql(wrapper.vm.allLookupTables)
            expect(allTableColMap).to.be.eql(wrapper.vm.allTableColMap)
            expect(refTargets).to.be.eql(wrapper.vm.refTargets)
            expect(tablesColNameMap).to.be.eql(wrapper.vm.tablesColNameMap)
            expect(tableId).to.be.eql(wrapper.vm.stagingData.id)
            expect(dim).to.be.eql(wrapper.vm.tabDim)
            expect(connData).to.be.eql(wrapper.vm.$props.connData)
            expect(charsetCollationMap).to.be.eql(wrapper.vm.charset_collation_map)
        })

        it(`Should not render fk-definitions if engine isn't supported`, () => {
            wrapper = mountFactory({
                computed: {
                    tblOpts: () => ({ ...editorDataStub.options, engine: 'MEMORY' }),
                    activeSpecTab: () => DDL_EDITOR_SPECS.FK,
                },
            })
            expect(wrapper.findComponent({ name: 'fk-definitions' }).exists()).to.be.false
        })

        it(`Should pass accurate data to index-definitions`, () => {
            wrapper = mountFactory({ computed: { activeSpecTab: () => DDL_EDITOR_SPECS.INDEXES } })
            const { value, tableColNameMap, dim, tableColMap } = wrapper.findComponent({
                name: 'index-definitions',
            }).vm.$props
            expect(value).to.be.eql(wrapper.vm.keyCategoryMap)
            expect(tableColNameMap).to.be.eql(
                wrapper.vm.tablesColNameMap[wrapper.vm.stagingData.id]
            )
            expect(dim).to.be.eql(wrapper.vm.tabDim)
            expect(tableColMap).to.be.eql(wrapper.vm.allTableColMap[wrapper.vm.stagingData.id])
        })

        const specCases = {
            [DDL_EDITOR_SPECS.COLUMNS]: 'col-definitions',
            [DDL_EDITOR_SPECS.FK]: 'fk-definitions',
            [DDL_EDITOR_SPECS.INDEXES]: 'index-definitions',
        }
        Object.keys(specCases).forEach(spec => {
            it(`Should render ${specCases[spec]} based on activeSpecTab`, () => {
                wrapper = mountFactory({ computed: { activeSpecTab: () => spec } })
                expect(wrapper.findComponent({ name: specCases[spec] }).exists()).to.be.true
            })
        })
    })

    it('Should conditionally render `apply-btn`', async () => {
        wrapper = mountFactory()
        expect(wrapper.findComponent({ name: 'apply-btn' }).exists()).to.be.true
        await wrapper.setProps({ showApplyBtn: false })
        expect(wrapper.findComponent({ name: 'apply-btn' }).exists()).to.be.false
    })

    it('Should not render `revert-btn` in creation mode', () => {
        wrapper = mountFactory({ propsData: { isCreating: true } })
        expect(wrapper.findComponent({ name: 'revert-btn' }).exists()).to.be.false
    })

    it('Should disables the `apply-btn` when no changes are made', () => {
        wrapper = mountFactory()
        const applyBtn = wrapper.findComponent({ name: 'apply-btn' })
        expect(applyBtn.vm.$attrs.disabled).to.be.true
    })

    it('renders toolbar-append slot content', () => {
        wrapper = mountFactory({ slots: { 'toolbar-append': '<div>toolbar-append-content</div>' } })
        expect(wrapper.html()).contains('<div>toolbar-append-content</div>')
    })

    it('Should validate the form before applying changes', () => {
        wrapper = mountFactory()
        const spy = sinon.spy(wrapper.vm.$refs.form, 'validate')
        wrapper.vm.onApply()
        spy.should.have.been.calledOnce
    })

    describe(`Computed properties and method tests`, () => {
        it('Should return accurate value for activeSpecTab', () => {
            wrapper = mountFactory({ computed: { activeSpecTab: () => DDL_EDITOR_SPECS.INDEXES } })
            expect(wrapper.vm.activeSpecTab).to.be.eql(DDL_EDITOR_SPECS.INDEXES)
        })

        it('Should emit update:activeSpec event', () => {
            wrapper = mountFactory()
            wrapper.vm.activeSpecTab = DDL_EDITOR_SPECS.FK
            expect(wrapper.emitted('update:activeSpec')[0]).to.be.eql([DDL_EDITOR_SPECS.FK])
        })

        it('Should return accurate value for stagingData', () => {
            wrapper = mountFactory()
            expect(wrapper.vm.stagingData).to.be.eql(wrapper.vm.$props.value)
        })

        it('Should emit input event', () => {
            wrapper = mountFactory()
            wrapper.vm.stagingData = null
            expect(wrapper.emitted('input')[0]).to.be.eql([null])
        })

        it('Should return accurate value for hasChanged when there is a diff', async () => {
            wrapper = mountFactory()
            expect(wrapper.vm.hasChanged).to.be.false
            await wrapper.setProps({ value: {} })
            expect(wrapper.vm.hasChanged).to.be.true
        })

        const computedDataTypeMap = {
            array: ['allLookupTables', 'knownTargets', 'refTargets'],
            object: [
                'stagingData',
                'tblOpts',
                'defs',
                'initialDefinitions',
                'keyCategoryMap',
                'editorDim',
                'tabDim',
                'knownTargetMap',
                'tablesColNameMap',
                'colKeyCategoryMap',
                'allTableColMap',
            ],
        }
        Object.keys(computedDataTypeMap).forEach(type => {
            computedDataTypeMap[type].forEach(property => {
                it(`${property} should return an ${type}`, () => {
                    wrapper = mountFactory()
                    expect(wrapper.vm[property]).to.be.an(type)
                })
            })
        })

        it('Should revert data', () => {
            wrapper = mountFactory()
            wrapper.vm.stagingData = {}
            wrapper.vm.onRevert()
            expect(wrapper.vm.stagingData).to.be.eql(wrapper.vm.$props.initialData)
        })
    })
})
