/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import SidebarCtr from '@wkeComps/QueryEditor/SidebarCtr.vue'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: SidebarCtr,
                propsData: {
                    queryEditorId: 'query-editor-id',
                    activeQueryTabId: 'query-tab-id',
                    activeQueryTabConn: {},
                    queryEditorTmp: {},
                },
            },
            opts
        )
    )

function mockShowingDbListTree() {
    return {
        isLoadingDbTree: () => false,
        hasConn: () => true,
    }
}

describe('sidebar-ctr', () => {
    let wrapper

    describe(`Child component's data communication tests`, () => {
        afterEach(() => sinon.restore())

        it(`Should pass accurate data to schema-tree-ctr via props`, () => {
            wrapper = mountFactory()
            const {
                queryEditorId,
                activeQueryTabId,
                queryEditorTmp,
                activeQueryTabConn,
                schemaSidebar,
                filterTxt,
            } = wrapper.findComponent({
                name: 'schema-tree-ctr',
            }).vm.$props
            expect(queryEditorId).to.equal(wrapper.vm.$props.queryEditorId)
            expect(activeQueryTabId).to.equal(wrapper.vm.$props.activeQueryTabId)
            expect(queryEditorTmp).to.eql(wrapper.vm.queryEditorTmp)
            expect(activeQueryTabConn).to.be.eql(wrapper.vm.$props.activeQueryTabConn)
            expect(schemaSidebar).to.eql(wrapper.vm.schemaSidebar)
            expect(filterTxt).to.equal(wrapper.vm.filterTxt)
        })

        const evtFnMap = {
            'get-node-data': 'fetchNodePrvwData',
            'load-children': 'handleLoadChildren',
            'use-db': 'useDb',
            'alter-tbl': 'onAlterTable',
            'drop-action': 'handleOpenExecSqlDlg',
            'truncate-tbl': 'handleOpenExecSqlDlg',
        }
        Object.keys(evtFnMap).forEach(evt => {
            it(`Should call ${evtFnMap[evt]} if ${evt} is emitted from schema-tree-ctr`, () => {
                const spyFn = sinon.spy(SidebarCtr.methods, evtFnMap[evt])
                wrapper = mountFactory({
                    computed: { ...mockShowingDbListTree() },
                })
                const dbListTree = wrapper.findComponent({ name: 'schema-tree-ctr' })
                dbListTree.vm.$emit(evt)
                spyFn.should.have.been.called
            })
        })
    })

    describe(`computed properties tests`, () => {
        let wrapper
        it(`Should return accurate value for hasConn`, () => {
            // have no connection
            wrapper = mountFactory()
            expect(wrapper.vm.hasConn).to.be.false
            // Have valid connection
            wrapper = mountFactory({ computed: { ...mockShowingDbListTree() } })
            expect(wrapper.vm.hasConn).to.be.true
        })
        it(`Should return accurate value for disableReload`, () => {
            // has connection
            wrapper = mountFactory({ computed: { hasConn: () => true } })
            expect(wrapper.vm.disableReload).to.be.false
            // have no connection and still loading for data
            wrapper = mountFactory({
                computed: { hasConn: () => false, isLoadingDbTree: () => true },
            })
            expect(wrapper.vm.disableReload).to.be.true
        })
    })

    describe(`Button tests`, () => {
        it(`Should disable reload-schemas button`, () => {
            wrapper = mountFactory({
                shallow: false,
                computed: { disableReload: () => true },
            })
            expect(wrapper.find('.reload-schemas').attributes().disabled).to.be.equals('disabled')
        })
        it(`Should disable filter-objects input`, () => {
            wrapper = mountFactory({
                shallow: false,
                computed: { disableReload: () => true },
            })
            expect(
                wrapper
                    .find('.filter-objects')
                    .find('input')
                    .attributes().disabled
            ).to.be.equals('disabled')
        })

        it(`Should call fetchSchemas when reload-schemas button is clicked`, async () => {
            let callCount = 0
            wrapper = mountFactory({
                shallow: false,
                computed: { disableReload: () => false, isCollapsed: () => false },
                methods: {
                    fetchSchemas: () => callCount++,
                },
            })
            await wrapper.find(`.reload-schemas`).trigger('click')
            expect(callCount).to.be.equals(1)
        })
    })
})
