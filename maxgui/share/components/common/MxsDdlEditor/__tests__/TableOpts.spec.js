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
import TableOpts from '@share/components/common/MxsDdlEditor/TableOpts'
import {
    editorDataStub,
    charsetCollationMapStub,
} from '@share/components/common/MxsDdlEditor/__tests__/stubData'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: TableOpts,
                propsData: {
                    value: editorDataStub.options,
                    engines: ['InnoDB'],
                    defDbCharset: 'utf8mb4',
                    charsetCollationMap: charsetCollationMapStub,
                    schemas: ['company'],
                    isCreating: false,
                },
            },
            opts
        )
    )

let wrapper

describe('table-opts', () => {
    describe(`Child component's data communication tests`, () => {
        const debouncedInputFields = ['name', 'comment']
        debouncedInputFields.forEach(field => {
            it(`Should pass accurate data to mxs-debounced-field ${field}`, () => {
                wrapper = mountFactory()
                const { value } = wrapper.findComponent(`.${field}`).vm.$attrs
                expect(value).to.be.eql(wrapper.vm.tblOpts[field])
            })
        })
        it(`Should pass accurate data to schemas dropdown`, () => {
            wrapper = mountFactory()
            const { value, items, disabled } = wrapper.findComponent('.schemas').vm.$props
            expect(value).to.be.eql(wrapper.vm.tblOpts.schema)
            expect(items).to.be.eql(wrapper.vm.$props.schemas)
            expect(disabled).to.be.eql(!wrapper.vm.$props.isCreating)
        })

        it(`Should pass accurate data to engines dropdown`, () => {
            wrapper = mountFactory()
            const { value, items } = wrapper.findComponent('.table-engine').vm.$props
            expect(value).to.be.eql(wrapper.vm.tblOpts.engine)
            expect(items).to.be.eql(wrapper.vm.$props.engines)
        })

        it(`Should pass accurate data to charset dropdown`, () => {
            wrapper = mountFactory()
            const {
                $attrs: { value, items },
                $props: { defItem },
            } = wrapper.findComponent('.charset').vm

            expect(value).to.be.eql(wrapper.vm.tblOpts.charset)
            expect(items).to.be.eql(Object.keys(wrapper.vm.$props.charsetCollationMap))
            expect(defItem).to.be.eql(wrapper.vm.$props.defDbCharset)
        })

        it(`Should pass accurate data to collation dropdown`, () => {
            wrapper = mountFactory()
            const {
                $attrs: { value, items },
                $props: { defItem },
            } = wrapper.findComponent('.collation').vm

            expect(value).to.be.eql(wrapper.vm.tblOpts.collation)
            expect(items).to.be.eql(
                wrapper.vm.$props.charsetCollationMap[wrapper.vm.tblOpts.charset].collations
            )
            expect(defItem).to.be.eql(wrapper.vm.defCollation)
        })
    })

    describe(`Computed properties tests`, () => {
        it('Should return accurate value for title', async () => {
            wrapper = mountFactory()
            expect(wrapper.vm.title).to.be.eql(wrapper.vm.$mxs_t('alterTbl'))
            await wrapper.setProps({ isCreating: true })
            expect(wrapper.vm.title).to.be.eql(wrapper.vm.$mxs_t('createTbl'))
        })

        it('Should return accurate value for tblOpts', () => {
            wrapper = mountFactory()
            expect(wrapper.vm.tblOpts).to.be.eql(wrapper.vm.$props.value)
        })

        it('Should emit input event', () => {
            wrapper = mountFactory()
            wrapper.vm.tblOpts = null
            expect(wrapper.emitted('input')[0]).to.be.eql([null])
        })

        it('Should return accurate value for defCollation', () => {
            wrapper = mountFactory()
            expect(wrapper.vm.defCollation).to.be.eql(
                wrapper.vm.charsetCollationMap[wrapper.vm.tblOpts.charset].defCollation
            )
        })
    })
})
