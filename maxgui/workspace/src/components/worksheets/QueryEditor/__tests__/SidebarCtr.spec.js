/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import SidebarCtr from '../SidebarCtr.vue'

const mountFactory = opts =>
    mount({
        shallow: true,
        component: SidebarCtr,
        stubs: {
            'sql-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

function mockShowingDbListTree() {
    return {
        isLoadingDbTree: () => false,
        hasConn: () => true,
    }
}

describe('sidebar-ctr', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
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
                spyFn.restore()
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
            wrapper = mountFactory({
                computed: { ...mockShowingDbListTree() },
            })
            expect(wrapper.vm.hasConn).to.be.true
        })
        it(`Should return accurate value for reloadDisabled`, async () => {
            // has connection
            wrapper = mountFactory({
                computed: {
                    hasConn: () => true,
                },
            })
            expect(wrapper.vm.reloadDisabled).to.be.false
            // have no connection and still loading for data
            await wrapper.setProps({ hasConn: false, isLoading: true })
            wrapper = mountFactory({
                computed: {
                    hasConn: () => false,
                    isLoadingDbTree: () => true,
                },
            })
            expect(wrapper.vm.reloadDisabled).to.be.true
        })
    })

    describe(`Button tests`, () => {
        it(`Should disable reload-schemas button`, () => {
            wrapper = mountFactory({
                shallow: false,
                computed: { reloadDisabled: () => true },
            })
            expect(wrapper.find('.reload-schemas').attributes().disabled).to.be.equals('disabled')
        })
        it(`Should disable filter-objects input`, () => {
            wrapper = mountFactory({
                shallow: false,
                computed: { reloadDisabled: () => true },
            })
            expect(
                wrapper
                    .find('.filter-objects')
                    .find('input')
                    .attributes().disabled
            ).to.be.equals('disabled')
        })

        const btnHandlerMap = {
            'reload-schemas': 'fetchSchemas',
            'toggle-sidebar': 'SET_IS_SIDEBAR_COLLAPSED',
        }
        Object.keys(btnHandlerMap).forEach(btn => {
            it(`Should call ${btnHandlerMap[btn]} when ${btn} button is clicked`, async () => {
                let callCount = 0
                wrapper = mountFactory({
                    shallow: false,
                    computed: { reloadDisabled: () => false, is_sidebar_collapsed: () => false },
                    methods: {
                        [btnHandlerMap[btn]]: () => callCount++,
                    },
                })
                await wrapper.find(`.${btn}`).trigger('click')
                expect(callCount).to.be.equals(1)
            })
        })
    })
})
