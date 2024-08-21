/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import SidebarCtr from '@wkeComps/QueryEditor/SidebarCtr.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    SidebarCtr,
    lodash.merge(
      {
        shallow: false,
        props: {
          queryEditorId: 'query-editor-id',
          queryEditorTmp: {},
          activeQueryTabId: 'query-tab-id',
          activeQueryTabConn: {},
          height: 500,
        },
      },
      opts
    )
  )

describe('sidebar-ctr', () => {
  let wrapper

  it(`Should pass expected data to SchemaTreeCtr`, () => {
    wrapper = mountFactory()
    const {
      $attrs: { height, search, loadChildren },
      $props: { queryEditorId, activeQueryTabId, queryEditorTmp, activeDb, schemaSidebar },
    } = wrapper.findComponent({
      name: 'SchemaTreeCtr',
    }).vm
    expect(height).toBe(wrapper.vm.schemaTreeHeight)
    expect(queryEditorId).toBe(wrapper.vm.$props.queryEditorId)
    expect(activeQueryTabId).toBe(wrapper.vm.$props.activeQueryTabId)
    expect(queryEditorTmp).toStrictEqual(wrapper.vm.queryEditorTmp)
    expect(activeDb).toStrictEqual(wrapper.vm.activeDb)
    expect(schemaSidebar).toStrictEqual(wrapper.vm.schemaSidebar)
    expect(search).toBe(wrapper.vm.filterTxt)
    expect(loadChildren).toStrictEqual(wrapper.vm.loadChildren)
  })

  it(`Should disable reload-schemas button`, () => {
    wrapper = mountFactory({ props: { queryEditorTmp: { loading_db_tree: true } } })
    expect(find(wrapper, 'reload-schemas').element.disabled).toBe(true)
  })

  it(`Should disable filter-objects input`, () => {
    wrapper = mountFactory({ props: { queryEditorTmp: { loading_db_tree: true } } })
    expect(find(wrapper, 'filter-objects').vm.$props.disabled).toBe(true)
  })
})
