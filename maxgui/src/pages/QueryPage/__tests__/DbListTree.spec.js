/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import DbListTree from '@/pages/QueryPage/DbListTree'
import { merge } from 'utils/helpers'

const mountFactory = opts =>
    mount(
        merge(
            {
                shallow: true,
                component: DbListTree,
                computed: { getDbTreeData: () => dummy_db_tree_data },
            },
            opts
        )
    )
const dummy_db_tree_data = [
    {
        key: 'node_key_0',
        type: 'Schema',
        name: 'mysql',
        id: 'mysql',
        data: {
            CATALOG_NAME: 'def',
            SCHEMA_NAME: 'mysql',
            DEFAULT_CHARACTER_SET_NAME: 'utf8mb4',
            DEFAULT_COLLATION_NAME: 'utf8mb4_general_ci',
            SQL_PATH: null,
            SCHEMA_COMMENT: '',
        },
        draggable: true,
        level: 0,
        isSys: true,
        children: [
            {
                key: 'node_key_1',
                type: 'Tables',
                name: 'Tables',
                id: 'mysql.Tables',
                draggable: false,
                level: 1,
                children: [],
            },
            {
                key: 'node_key_2',
                type: 'Stored Procedures',
                name: 'Stored Procedures',
                id: 'mysql.Stored Procedures',
                draggable: false,
                level: 1,
                children: [],
            },
        ],
    },
    {
        key: 'node_key_3',
        type: 'Schema',
        name: 'test',
        id: 'test',
        data: {
            CATALOG_NAME: 'def',
            SCHEMA_NAME: 'test',
            DEFAULT_CHARACTER_SET_NAME: 'utf8mb4',
            DEFAULT_COLLATION_NAME: 'utf8mb4_general_ci',
            SQL_PATH: null,
            SCHEMA_COMMENT: '',
        },
        draggable: true,
        level: 0,
        isSys: false,
        children: [
            {
                key: 'node_key_4',
                type: 'Tables',
                name: 'Tables',
                id: 'test.Tables',
                draggable: false,
                level: 1,
                children: [],
            },
            {
                key: 'node_key_5',
                type: 'Stored Procedures',
                name: 'Stored Procedures',
                id: 'test.Stored Procedures',
                draggable: false,
                level: 1,
                children: [],
            },
        ],
    },
]
describe(`DbListTree - m-treeview tests`, () => {
    let wrapper
    afterEach(() => wrapper.destroy())
    it(`Should not render m-treeview component if there is no data`, () => {
        wrapper = mountFactory({ computed: { getDbTreeData: () => [] } })
        expect(wrapper.find('.m-treeview').exists()).to.be.false
    })
    it(`Should pass accurate data to m-treeview via props`, () => {
        wrapper = mountFactory()
        const {
            items,
            search,
            filter,
            hoverable,
            openOnClick,
            loadChildren,
            active,
            open,
            returnObject,
        } = wrapper.find('.m-treeview').vm.$props
        expect(items).to.be.deep.equals(dummy_db_tree_data)
        expect(search).to.be.equals(wrapper.vm.search_schema)
        expect(filter).to.be.equals(wrapper.vm.filter)
        expect(hoverable).to.be.true
        expect(openOnClick).to.be.true
        expect(loadChildren).to.be.equal(wrapper.vm.handleLoadChildren)
        expect(active).to.be.deep.equals(wrapper.vm.activeNodes)
        expect(open).to.be.deep.equals(wrapper.vm.$data.expandedNodes)
        expect(returnObject).to.be.true
    })

    const fnEvtMap = {
        onNodeClick: 'item:click',
        onContextMenu: 'item:contextmenu',
    }
    Object.keys(fnEvtMap).forEach(fn => {
        it(`Should call ${fn} when ${fnEvtMap[fn]} event is emitted
          from m-treeview component`, () => {
            let fnSpy = sinon.spy(DbListTree.methods, fn)
            wrapper = mountFactory({ methods: { handleOpenCtxMenu: () => null } })
            let param = dummy_db_tree_data[0]
            if (fnEvtMap[fn] === 'item:contextmenu')
                param = { e: 'Event', item: dummy_db_tree_data[0] }
            wrapper.find('.m-treeview').vm.$emit(fnEvtMap[fn], param)
            fnSpy.should.have.been.calledOnceWith(param)
            fnSpy.restore()
        })
    })
    it(`Should bold SCHEMA type node if active_db value equals to the name of that node`, () => {
        const schemaNode = dummy_db_tree_data.find(node => node.type === 'Schema')
        wrapper = mountFactory({
            shallow: false,
            computed: {
                active_db: () => schemaNode.name,
            },
        })
        const schemaNodeNameEle = wrapper
            .find('.m-treeview')
            .find(`#node-tooltip-activator-${schemaNode.key}`)
            .find('.node-name')
        expect(schemaNodeNameEle.classes()).to.include.members(['font-weight-bold'])
    })
})
describe(`DbListTree - node tooltip tests`, () => {
    let wrapper
    afterEach(() => wrapper.destroy())
    it(`Should pass accurate data to v-tooltip via props`, () => {
        wrapper = mountFactory({ data: () => ({ hoveredItem: dummy_db_tree_data[0] }) })
        const { value, disabled, right, nudgeRight, activator } = wrapper.findComponent({
            name: 'v-tooltip',
        }).vm.$props
        expect(value).to.be.true // true since hoveredItem has value
        expect(disabled).to.be.equals(wrapper.vm.$data.isDragging)
        expect(right).to.be.true
        expect(nudgeRight).to.be.equals(45)
        expect(activator).to.be.equals(`#node-tooltip-activator-${dummy_db_tree_data[0].key}`)
    })
    it(`Should add an id attribute accurately to each first node level`, () => {
        wrapper = mountFactory({ shallow: false })
        const nodeKeys = dummy_db_tree_data.map(node => node.key)
        nodeKeys.forEach(key => {
            expect(
                wrapper
                    .find('.m-treeview')
                    .find(`#node-tooltip-activator-${key}`)
                    .exists()
            ).to.be.true
        })
    })
    it(`Should assign hovered item to hoveredItem when item:hovered event is emitted
    from m-treeview component`, () => {
        wrapper = mountFactory()
        wrapper.find('.m-treeview').vm.$emit('item:hovered', dummy_db_tree_data[0])
        expect(wrapper.vm.$data.hoveredItem).to.be.deep.equals(dummy_db_tree_data[0])
    })
})
describe(`DbListTree - draggable node tests`, () => {
    let wrapper
    afterEach(() => wrapper.destroy())
    it(`Should call onNodeDragStart when mousedown event is emitted`, () => {
        let evtParam
        wrapper = mountFactory({
            shallow: false,
            methods: { onNodeDragStart: e => (evtParam = e) },
        })
        sinon.spy(wrapper.vm, 'onNodeDragStart')
        const draggableNode = dummy_db_tree_data.find(node => node.draggable)
        const draggableNodeEle = wrapper
            .find('.m-treeview')
            .find(`#node-tooltip-activator-${draggableNode.key}`)
        draggableNodeEle.trigger('mousedown')
        wrapper.vm.onNodeDragStart.should.have.been.calledOnceWith(evtParam)
    })
})
//TODO: Add context menu tests and computed and method tests
