/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import SchemaTreeCtr from '@wkeComps/QueryEditor/SchemaTreeCtr.vue'
import { lodash } from '@share/utils/helpers'
import { NODE_CTX_TYPES, NODE_TYPES } from '@wsSrc/constants'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: SchemaTreeCtr,
                computed: { dbTreeData: () => dummy_db_tree_data },
                propsData: {
                    queryEditorId: 'query-editor-id',
                    activeQueryTabId: 'query-tab-id',
                    queryEditorTmp: {},
                    activeQueryTabConn: {},
                    filterTxt: '',
                    schemaSidebar: {},
                },
            },
            opts
        )
    )
//TODO: create a stub function
const dummy_db_tree_data = [
    {
        key: 'node_key_0',
        type: 'SCHEMA',
        name: 'mysql',
        id: 'mysql',
        qualified_name: '`mysql`',
        parentNameData: { SCHEMA: 'mysql' },
        data: {
            CATALOG_NAME: 'def',
            SCHEMA_NAME: 'mysql',
            DEFAULT_CHARACTER_SET_NAME: 'utf8mb4',
            DEFAULT_COLLATION_NAME: 'utf8mb4_general_ci',
            SQL_PATH: null,
            SCHEMA_COMMENT: '',
        },
        draggable: true,
        level: 1,
        isSys: true,
        children: [
            {
                key: 'node_key_1',
                type: 'Tables',
                name: 'Tables',
                id: 'mysql.Tables',
                qualified_name: '`mysql`.`Tables`',
                draggable: false,
                level: 2,
                children: [],
            },
            {
                key: 'node_key_2',
                type: 'Stored Procedures',
                name: 'Stored Procedures',
                id: 'mysql.Stored Procedures',
                qualified_name: '`mysql`.`Stored Procedures`',
                draggable: false,
                level: 2,
                children: [],
            },
        ],
    },
    {
        key: 'node_key_3',
        type: 'SCHEMA',
        name: 'test',
        id: 'test',
        qualified_name: '`test`',
        parentNameData: { SCHEMA: 'test' },
        data: {
            CATALOG_NAME: 'def',
            SCHEMA_NAME: 'test',
            DEFAULT_CHARACTER_SET_NAME: 'utf8mb4',
            DEFAULT_COLLATION_NAME: 'utf8mb4_general_ci',
            SQL_PATH: null,
            SCHEMA_COMMENT: '',
        },
        draggable: true,
        level: 1,
        isSys: false,
        children: [
            {
                key: 'node_key_4',
                type: 'Tables',
                name: 'Tables',
                id: 'test.Tables',
                qualified_name: '`test`.`Tables`',
                draggable: false,
                level: 2,
                children: [],
            },
            {
                key: 'node_key_5',
                type: 'Stored Procedures',
                name: 'Stored Procedures',
                id: 'test.Stored Procedures',
                qualified_name: '`test`.`Stored Procedures`',
                draggable: false,
                level: 2,
                children: [],
            },
        ],
    },
]
const dummy_schema_node = dummy_db_tree_data[0]
const dummy_tbl_node = {
    id: 'test.Tables.t1',
    qualified_name: '`test`.`t1`',
    type: 'TABLE',
    name: 't1',
    draggable: true,
    children: [],
}

describe(`SchemaTreeCtr`, () => {
    let wrapper

    afterEach(() => sinon.restore())

    it(`Should not render mxs-treeview component if there is no data`, () => {
        wrapper = mountFactory({ computed: { dbTreeData: () => [] } })
        expect(wrapper.find('.mxs-treeview').exists()).to.be.false
    })
    it(`Should pass accurate data to mxs-treeview via props`, () => {
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
        } = wrapper.find('.mxs-treeview').vm.$props
        expect(items).to.be.deep.equals(dummy_db_tree_data)
        expect(search).to.be.equals(wrapper.vm.filterTxt)
        expect(filter).to.be.equals(wrapper.vm.filter)
        expect(hoverable).to.be.true
        expect(openOnClick).to.be.true
        expect(loadChildren).to.be.equal(wrapper.vm.handleLoadChildren)
        expect(active).to.be.deep.equals(wrapper.vm.activeNodes)
        expect(open).to.be.deep.equals(wrapper.vm.expandedNodes)
        expect(returnObject).to.be.true
    })

    const fnEvtMap = {
        onContextMenu: 'item:contextmenu',
    }
    Object.keys(fnEvtMap).forEach(fn => {
        it(`Should call ${fn} when ${fnEvtMap[fn]} event is emitted
          from mxs-treeview component`, () => {
            let fnSpy = sinon.spy(SchemaTreeCtr.methods, fn)
            wrapper = mountFactory({ methods: { handleOpenCtxMenu: () => null } })
            let param = dummy_schema_node
            if (fnEvtMap[fn] === 'item:contextmenu') param = { e: 'Event', item: dummy_schema_node }
            wrapper.find('.mxs-treeview').vm.$emit(fnEvtMap[fn], param)
            fnSpy.should.have.been.calledOnceWith(param)
        })
    })
    it(`Should bold SCHEMA node if active_db === node.qualified_name`, () => {
        wrapper = mountFactory({
            shallow: false,
            propsData: { activeQueryTabConn: { active_db: dummy_schema_node.qualified_name } },
        })
        const schemaNodeNameEle = wrapper
            .find('.mxs-treeview')
            .find(`#node-tooltip-activator-${dummy_schema_node.key}`)
            .find('.node-name')
        expect(schemaNodeNameEle.classes()).to.include.members(['font-weight-bold'])
    })

    describe(`node tooltip tests`, () => {
        it(`Should pass accurate data to preview-data-tooltip via props`, () => {
            wrapper = mountFactory({ data: () => ({ hoveredNode: dummy_schema_node }) })
            const { activator } = wrapper.findComponent('.preview-data-tooltip').vm.$props
            expect(activator).to.be.equals(`#prvw-btn-tooltip-activator-${dummy_schema_node.key}`)
        })
        it(`Should pass accurate data to node-tooltip via props`, () => {
            wrapper = mountFactory({ data: () => ({ hoveredNode: dummy_schema_node }) })
            const { value, disabled, right, nudgeRight, activator } = wrapper.findComponent(
                '.node-tooltip'
            ).vm.$props
            expect(value).to.be.true // true since hoveredNode has value
            expect(disabled).to.be.equals(wrapper.vm.$data.isDragging)
            expect(right).to.be.true
            expect(nudgeRight).to.be.equals(45)
            expect(activator).to.be.equals(`#node-tooltip-activator-${dummy_schema_node.key}`)
        })
        it(`Should add an id attribute accurately to each node`, () => {
            wrapper = mountFactory({ shallow: false })
            const nodeKey = dummy_schema_node.key
            expect(
                wrapper
                    .find('.mxs-treeview')
                    .find(`#node-tooltip-activator-${nodeKey}`)
                    .exists()
            ).to.be.true
        })
        it(`Should assign hovered item to hoveredNode when item:hovered event is emitted
        from mxs-treeview component`, () => {
            wrapper = mountFactory()
            wrapper.find('.mxs-treeview').vm.$emit('item:hovered', dummy_schema_node)
            expect(wrapper.vm.$data.hoveredNode).to.be.deep.equals(dummy_schema_node)
        })
    })
    describe(`draggable node tests`, () => {
        afterEach(() => sinon.restore())

        it(`Should call onNodeDragStart when mousedown event is emitted`, () => {
            let evtParam
            wrapper = mountFactory({
                shallow: false,
                methods: { onNodeDragStart: e => (evtParam = e) },
            })
            sinon.spy(wrapper.vm, 'onNodeDragStart')
            const draggableNode = dummy_schema_node
            const draggableNodeEle = wrapper
                .find('.mxs-treeview')
                .find(`#node-tooltip-activator-${draggableNode.key}`)
            draggableNodeEle.trigger('mousedown')
            wrapper.vm.onNodeDragStart.should.have.been.calledOnceWith(evtParam)
        })
    })
    describe(`context menu tests`, () => {
        afterEach(() => sinon.restore())

        /**
         * @param {Object} param.wrapper - mounted wrapper (not shallow mount)
         *  @param {Object} param.node - tree node
         * @returns {Object} - more option icon
         */
        async function getMoreOptIcon({ wrapper, node }) {
            const target = wrapper.find(`#node-tooltip-activator-${node.key}`)
            await target.trigger('mouseover') // show more option icon button
            return wrapper.find(`#ctx-menu-activator-${node.key}`)
        }
        it(`Should pass accurate data to mxs-sub-menu via props`, async () => {
            // condition to render the menu
            wrapper = mountFactory({ data: () => ({ activeCtxNode: dummy_schema_node }) })
            await wrapper.setData({ activeCtxItemOpts: wrapper.vm.genNodeOpts(dummy_schema_node) })
            const menu = wrapper.findComponent({ name: 'mxs-sub-menu' })
            const { value, left, activator } = menu.vm.$attrs
            const { items } = menu.vm.$props
            expect(value).to.be.equals(wrapper.vm.showCtxMenu)
            expect(left).to.be.true
            expect(items).to.be.deep.equals(wrapper.vm.$data.activeCtxItemOpts)
            expect(activator).to.be.equals(`#ctx-menu-activator-${dummy_schema_node.key}`)
            expect(menu.vm.$vnode.key).to.be.equals(dummy_schema_node.key)
        })
        it(`Should handle @item-click event emitted from mxs-sub-menu`, () => {
            wrapper = mountFactory({ data: () => ({ activeCtxNode: dummy_schema_node }) })
            const fnSpy = sinon.spy(wrapper.vm, 'optionHandler')
            const menu = wrapper.findComponent({ name: 'mxs-sub-menu' })
            const mockOpt = { text: 'Qualified Name', type: 'INSERT' }
            menu.vm.$emit('item-click', mockOpt)
            fnSpy.should.have.been.calledOnceWithExactly({ node: dummy_schema_node, opt: mockOpt })
        })
        it(`Should show more option icon button when hovering a tree node`, async () => {
            wrapper = mountFactory({ shallow: false })
            const btn = await getMoreOptIcon({ wrapper, node: dummy_schema_node })
            expect(btn.exists()).to.be.true
        })
        it(`Should call handleOpenCtxMenu when clicking more option icon button`, async () => {
            wrapper = mountFactory({ shallow: false })
            const fnSpy = sinon.spy(wrapper.vm, 'handleOpenCtxMenu')
            const btn = await getMoreOptIcon({ wrapper, node: dummy_schema_node })
            await btn.trigger('click')
            fnSpy.should.have.been.calledOnce
        })
        it(`Should return base opts for system node when calling genNodeOpts method`, () => {
            wrapper = mountFactory()
            const sysNode = dummy_db_tree_data.find(node => node.isSys)
            expect(wrapper.vm.genNodeOpts(sysNode)).to.be.deep.equals(
                wrapper.vm.baseOptsMap[sysNode.type]
            )
        })
        it(`Should return accurate opts for user node when calling genNodeOpts method`, () => {
            wrapper = mountFactory()
            const userNode = dummy_db_tree_data.find(node => !node.isSys)
            const expectOpts = [
                ...wrapper.vm.baseOptsMap[userNode.type],
                { divider: true },
                ...wrapper.vm.genUserNodeOpts(userNode),
            ]
            expect(wrapper.vm.genNodeOpts(userNode)).to.be.deep.equals(expectOpts)
        })

        const mockOpts = Object.values(NODE_CTX_TYPES).map(type => ({ type }))
        mockOpts.forEach(opt => {
            it(`optionHandler should emit event as expected if context type is ${opt.type}`, () => {
                wrapper = mountFactory()
                const node = opt.type === 'Use' ? dummy_schema_node : dummy_tbl_node
                let fnSpy = sinon.spy(wrapper.vm, 'handleTxtOpt')
                wrapper.vm.optionHandler({ node, opt })
                switch (opt.type) {
                    case NODE_CTX_TYPES.USE:
                        expect(wrapper.emitted()['use-db'][0][0]).to.be.eql(
                            dummy_schema_node.qualified_name
                        )
                        break
                    case NODE_CTX_TYPES.PRVW_DATA:
                    case NODE_CTX_TYPES.PRVW_DATA_DETAILS:
                        expect(wrapper.emitted()['get-node-data'][0][0]).to.be.eql({
                            query_mode: opt.type,
                            qualified_name: node.qualified_name,
                        })
                        break
                    case NODE_CTX_TYPES.INSERT:
                    case NODE_CTX_TYPES.CLIPBOARD:
                        fnSpy.should.have.been.calledWith({ node, opt })
                        break
                    case NODE_CTX_TYPES.DROP:
                        expect(wrapper.emitted()['drop-action'][0][0]).to.be.eql(
                            'DROP ' + dummy_tbl_node.type + ' `test`.`t1`;'
                        )
                        break
                    case NODE_CTX_TYPES.ALTER:
                        expect(wrapper.emitted()['alter-tbl'][0][0]).to.be.eql(
                            wrapper.vm.minimizeNode(dummy_tbl_node)
                        )
                        break
                    case NODE_CTX_TYPES.TRUNCATE:
                        expect(wrapper.emitted()['truncate-tbl'][0][0]).to.be.eql(
                            'TRUNCATE TABLE `test`.`t1`;'
                        )
                        break
                }
            })
        })
    })
    describe(`computed and other method tests`, () => {
        let wrapper

        afterEach(() => sinon.restore())

        it(`Should return accurate value for nodesHaveCtxMenu computed property`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.nodesHaveCtxMenu).to.eql(Object.values(NODE_TYPES))
        })
        Object.values(NODE_TYPES).forEach(type => {
            it(`baseOptsMap should return accurate value for node type ${type} `, () => {
                wrapper = mountFactory({
                    computed: { isSqlEditor: () => true },
                })
                const { SCHEMA, TBL, VIEW, SP, FN, COL, TRIGGER } = NODE_TYPES
                const { USE, PRVW_DATA, PRVW_DATA_DETAILS, VIEW_INSIGHTS, GEN_ERD } = NODE_CTX_TYPES
                switch (type) {
                    case SCHEMA:
                        expect(wrapper.vm.baseOptsMap[type]).to.eql([
                            { disabled: false, text: wrapper.vm.$mxs_t('useDb'), type: USE },
                            { text: wrapper.vm.$mxs_t('viewInsights'), type: VIEW_INSIGHTS },
                            { text: wrapper.vm.$mxs_t('genErd'), type: GEN_ERD },
                            ...wrapper.vm.txtOpts,
                        ])
                        break
                    case TBL:
                        expect(wrapper.vm.baseOptsMap[type]).to.eql([
                            {
                                text: wrapper.vm.$mxs_t('previewData'),
                                type: PRVW_DATA,
                                disabled: false,
                            },
                            {
                                text: wrapper.vm.$mxs_t('viewDetails'),
                                type: PRVW_DATA_DETAILS,
                                disabled: false,
                            },
                            { text: wrapper.vm.$mxs_t('viewInsights'), type: VIEW_INSIGHTS },
                            { divider: true },
                            ...wrapper.vm.txtOpts,
                        ])
                        break
                    case VIEW:
                        expect(wrapper.vm.baseOptsMap[type]).to.eql([
                            {
                                text: wrapper.vm.$mxs_t('previewData'),
                                type: PRVW_DATA,
                                disabled: false,
                            },
                            {
                                text: wrapper.vm.$mxs_t('viewDetails'),
                                type: PRVW_DATA_DETAILS,
                                disabled: false,
                            },
                            { text: wrapper.vm.$mxs_t('showCreate'), type: VIEW_INSIGHTS },
                            { divider: true },
                            ...wrapper.vm.txtOpts,
                        ])
                        break
                    case COL:
                        expect(wrapper.vm.baseOptsMap[type]).to.eql(wrapper.vm.txtOpts)
                        break
                    case SP:
                    case FN:
                    case TRIGGER:
                        expect(wrapper.vm.baseOptsMap[type]).to.eql([
                            { text: wrapper.vm.$mxs_t('showCreate'), type: VIEW_INSIGHTS },
                            { divider: true },
                            ...wrapper.vm.txtOpts,
                        ])
                        break
                }
            })
        })
        it(`Should return accurate value for txtOpts computed property`, () => {
            wrapper = mountFactory()
            expect(wrapper.vm.txtOpts).to.eql([
                {
                    text: wrapper.vm.$mxs_t('placeToEditor'),
                    children: wrapper.vm.genTxtOpts(NODE_CTX_TYPES.INSERT),
                },
                {
                    text: wrapper.vm.$mxs_t('copyToClipboard'),
                    children: wrapper.vm.genTxtOpts(NODE_CTX_TYPES.CLIPBOARD),
                },
            ])
        })

        const expectedBoolVal = [true, false]
        expectedBoolVal.forEach(v => {
            it(`Should return ${v} when filter method is called`, () => {
                wrapper = mountFactory()
                const mockSearchItem = dummy_db_tree_data.find(node => node.name === 'mysql')
                const textKey = 'name'
                const searchKeyword = v ? 'sql' : 'test'
                expect(wrapper.vm.filter(mockSearchItem, searchKeyword, textKey)).to.be[v]
            })
            it(`Should return ${v} when showCtxBtn method is called`, () => {
                const node = dummy_schema_node
                wrapper = mountFactory({
                    data: () => ({
                        // mock activeCtxNode
                        activeCtxNode: v ? node : null,
                    }),
                })
                expect(wrapper.vm.showCtxBtn(node)).to.be[v]
            })
        })
        it(`Should return nodes with less properties when minimizeNode is called`, () => {
            const node = dummy_schema_node
            wrapper = mountFactory()
            const minimizeNode = wrapper.vm.minimizeNode(node)
            expect(Object.keys(minimizeNode).length).to.be.below(Object.keys(node).length)
            expect(minimizeNode).to.be.eql({
                id: node.id,
                qualified_name: node.qualified_name,
                parentNameData: { SCHEMA: node.name },
                name: node.name,
                type: node.type,
                level: node.level,
            })
        })
        it(`Should emit load-children event when handleLoadChildren is called`, async () => {
            const tablesNode = dummy_schema_node.children[0]
            let isEmitted = false
            let expectedArg
            const handler = param => {
                isEmitted = true
                expectedArg = param
            }
            wrapper = mountFactory({
                // handleLoadChildren uses listeners instead of emit
                listeners: { 'load-children': handler },
            })
            await wrapper.vm.handleLoadChildren(tablesNode)
            expect(isEmitted).to.be.true
            expect(expectedArg).to.be.eql(tablesNode)
        })

        const txtOptTypes = [NODE_CTX_TYPES.INSERT, NODE_CTX_TYPES.CLIPBOARD]
        txtOptTypes.forEach(type => {
            it(`genTxtOpts should return valid text option for context type ${type}`, () => {
                wrapper = mountFactory()
                expect(wrapper.vm.genTxtOpts(type)).to.be.eql([
                    { text: wrapper.vm.$mxs_t('qualifiedName'), type },
                    { text: wrapper.vm.$mxs_t('nameQuoted'), type },
                    { text: wrapper.vm.$mxs_t('name'), type },
                ])
            })
        })
        const mockTxtOptStrs = [
            'Qualified Name (Quoted)',
            'Qualified Name',
            'Name (Quoted)',
            'Name',
        ]

        mockTxtOptStrs.forEach(text => {
            it(`Should process handleTxtOpt as expected when the user
            selects ${text} option`, () => {
                wrapper = mountFactory()
                txtOptTypes.forEach(type => {
                    let copySpy
                    if (type === 'CLIPBOARD') copySpy = sinon.spy(document, 'execCommand')
                    wrapper.vm.handleTxtOpt({ node: dummy_schema_node, opt: { text, type } })
                    switch (type) {
                        case 'INSERT':
                            expect(wrapper.emitted()).to.have.property('place-to-editor')
                            if (text.includes('(Quoted)'))
                                expect(wrapper.emitted()['place-to-editor'][0][0]).to.be.eql(
                                    wrapper.vm.$helpers.quotingIdentifier(dummy_schema_node.name)
                                )
                            else
                                expect(wrapper.emitted()['place-to-editor'][0][0]).to.be.eql(
                                    dummy_schema_node.name
                                )
                            break
                        case 'CLIPBOARD':
                            copySpy.should.have.been.calledOnceWith('copy')
                            break
                    }
                })
            })
        })
    })
})
