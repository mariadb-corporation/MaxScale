/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import App from '@/App'

const dummyValues = {
    undefined: undefined,
    null: null,
    true: true,
    false: false,
    emptyString: '',
    string: 'string',
    number: 0,
    emptyObj: {},
    validObj: { keyName: 'keyValue' },
    emptyArr: [],
    validArr: [0, 1, 2, 3],
    validArrObj: [{ keyName: 'keyValue' }],
}

describe('helpers unit tests', () => {
    it('Should add $help to vue prototype methods', () => {
        let wrapper = mount({
            shallow: false,
            component: App,
        })
        expect(wrapper.vm.$help).to.be.not.undefined
        wrapper.destroy()
    })

    const helper = Vue.prototype.$help

    describe('isNotEmptyObj assertions', () => {
        for (const [key, value] of Object.entries(dummyValues)) {
            let expectResult = key === 'validObj'
            it(`Should return ${expectResult} when value is ${key}`, () => {
                expect(helper.isNotEmptyObj(value)).to.be[expectResult]
            })
        }
    })

    describe('isNotEmptyArray assertions', () => {
        for (const [key, value] of Object.entries(dummyValues)) {
            let expectResult = key === 'validArr' || key === 'validArrObj'
            it(`Should return ${expectResult} when value is ${key}`, () => {
                expect(helper.isNotEmptyArray(value)).to.be[expectResult]
            })
        }
    })

    it(`Should return token_body cookie when getCookie is called`, () => {
        document.cookie = 'token_body=eyJhbGcI1NiJ9.eyJhdWsZSJ9; SameSite=Lax'
        expect(helper.getCookie('token_body')).to.be.not.undefined
    })

    describe('serviceStateIcon assertions', () => {
        const dummyServiceStates = ['Started', 'Stopped', 'Allocated', 'Failed', 'Unknown']
        const expectedReturn = [1, 2, 0, 0, '']
        dummyServiceStates.forEach((state, i) => {
            let des = `Should return ${expectedReturn[i]} when state is ${state}`
            if (state === 'Unknown')
                des = des.replace(
                    `Should return ${expectedReturn[i]}`,
                    'Should return empty string (bug icon will be rendered)'
                )
            it(des, () => {
                expect(helper.serviceStateIcon(state)).to.be.equals(expectedReturn[i])
            })
        })
    })

    describe('serverStateIcon assertions', () => {
        const dummyServerStates = [
            'Running',
            'Down',
            'Master, Running',
            'Slave, Running',
            'Synced, Running',
            'Drained, Slave, Running',
            'Maintenance, Down',
            'Maintenance, Running',
        ]
        const expectedReturn = [0, 0, 1, 1, 1, 1, 2, 2]
        dummyServerStates.forEach((state, i) => {
            it(`Should return ${expectedReturn[i]} when state is ${state}`, () => {
                expect(helper.serverStateIcon(state)).to.be.equals(expectedReturn[i])
            })
        })
    })

    describe('monitorStateIcon assertions', () => {
        const dummyServiceStates = ['Running', 'Stopped', 'Unknown']
        const expectedReturn = [1, 2, '']
        dummyServiceStates.forEach((state, i) => {
            let des = `Should return ${expectedReturn[i]} when state is ${state}`
            if (state === 'Unknown')
                des = des.replace(
                    `Should return ${expectedReturn[i]}`,
                    'Should return empty string (bug icon will be rendered)'
                )
            it(des, () => {
                expect(helper.monitorStateIcon(state)).to.be.equals(expectedReturn[i])
            })
        })
    })

    describe('listenerStateIcon assertions', () => {
        const dummyServiceStates = ['Failed', 'Running', 'Stopped', 'Unknown']
        const expectedReturn = [0, 1, 2, '']
        dummyServiceStates.forEach((state, i) => {
            let des = `Should return ${expectedReturn[i]} when state is ${state}`
            if (state === 'Unknown')
                des = des.replace(
                    `Should return ${expectedReturn[i]}`,
                    'Should return empty string (bug icon will be rendered)'
                )
            it(des, () => {
                expect(helper.listenerStateIcon(state)).to.be.equals(expectedReturn[i])
            })
        })
    })

    it('strReplaceAt should return new string accurately', () => {
        expect(helper.strReplaceAt({ str: 'Servers', index: 6, newChar: '' })).to.be.equals(
            'Server'
        )
        expect(
            helper.strReplaceAt({ str: 'rgba(171,199,74,1)', index: 16, newChar: '0.1' })
        ).to.be.equals('rgba(171,199,74,0.1)')
    })

    it(`getErrorsArr should always return array of error strings regardless
      provided argument is an object or string`, () => {
        const dummy_response_errors = [
            {
                response: {
                    data: {
                        errors: [
                            {
                                detail: 'Error message 0',
                            },
                            {
                                detail: 'Error message 1',
                            },
                        ],
                    },
                },
            },
            'Just string error',
        ]
        const expectedReturn = [['Error message 0', 'Error message 1'], ['Just string error']]
        dummy_response_errors.forEach((error, i) => {
            let result = helper.getErrorsArr(error)
            expect(result).to.be.an('array')
            expect(result).to.be.deep.equals(expectedReturn[i])
        })
    })

    it('hashMapByPath should return hash map', () => {
        const dummy_arr = [
            {
                id: 'server_0',
                groupId: 'Monitor',
            },
            {
                id: 'server_1',
                groupId: 'Monitor',
            },
            {
                id: 'server_2',
                groupId: 'galeramon-monitor',
            },
            {
                id: 'server_3',
                groupId: 'Not Monitored',
            },
        ]
        const expectReturn = {
            Monitor: [
                { id: 'server_0', groupId: 'Monitor' },
                { id: 'server_1', groupId: 'Monitor' },
            ],
            'galeramon-monitor': [{ id: 'server_2', groupId: 'galeramon-monitor' }],
            'Not Monitored': [{ id: 'server_3', groupId: 'Not Monitored' }],
        }

        expect(helper.hashMapByPath({ arr: dummy_arr, path: 'groupId' })).to.be.deep.equals(
            expectReturn
        )
    })

    describe('dateFormat assertions', () => {
        const dummyValue = 'Tue, 01 Sep 2020 05:55:56 GMT'
        const dummyFormatTypes = ['DATE_RFC2822', 'MM.DD.YYYY HH:mm:ss', undefined]
        const expectedReturn = [
            'Tue, 01 Sep 2020 08:55:56',
            '09.01.2020 08:55:56',
            '08:55:56 09.01.2020',
        ]
        dummyFormatTypes.forEach((type, i) => {
            let des = `Should return date string with format ${type}`
            if (type === undefined)
                des = des.replace(`format ${type}`, 'default format HH:mm:ss MM.DD.YYYY')
            it(des, () => {
                expect(helper.dateFormat({ value: dummyValue, formatType: type })).to.be.equals(
                    expectedReturn[i]
                )
            })
        })
    })

    const dummyTree = {
        root_node: {
            node_child: { grand_child: 'grand_child value' },
            node_child_1: 'node_child_1 value',
        },
        root_node_1: 'root_node_1 value',
    }
    const treeArrStub = [
        {
            nodeId: 1,
            parentNodeId: 0,
            level: 0,
            id: 'root_node',
            value: '',
            originalValue: dummyTree.root_node,
            expanded: false,
            children: [
                {
                    nodeId: 2,
                    parentNodeId: 1,
                    level: 1,
                    id: 'node_child',
                    value: '',
                    originalValue: dummyTree.root_node.node_child,
                    expanded: false,
                    children: [
                        {
                            nodeId: 3,
                            parentNodeId: 2,
                            level: 2,
                            id: 'grand_child',
                            value: 'grand_child value',
                            originalValue: 'grand_child value',
                            leaf: true,
                        },
                    ],
                    leaf: false,
                },
                {
                    nodeId: 4,
                    parentNodeId: 1,
                    level: 1,
                    id: 'node_child_1',
                    value: 'node_child_1 value',
                    originalValue: 'node_child_1 value',
                    leaf: true,
                },
            ],
            leaf: false,
        },
        {
            nodeId: 5,
            parentNodeId: 0,
            level: 0,
            id: 'root_node_1',
            value: 'root_node_1 value',
            originalValue: 'root_node_1 value',
            leaf: true,
        },
    ]

    it(`Should return flattened tree when flattenTree is called`, () => {
        const flattened = helper.flattenTree(treeArrStub)
        const lastNodeId = treeArrStub[treeArrStub.length - 1].nodeId
        expect(flattened.length).to.be.equals(lastNodeId)
        flattened.forEach(node => {
            if (node.children) expect(node.expanded).to.be.true
        })
    })

    it(`Should return ancestor nodeId of a node when findAncestor is called`, () => {
        const expectAncestorNodeId = treeArrStub[0].nodeId
        const nodeStub = treeArrStub[0].children[0].children[0]
        let treeMapMock = new Map()
        const flattened = helper.flattenTree(treeArrStub)
        flattened.forEach(node => treeMapMock.set(node.nodeId, node))
        const ancestorId = helper.findAncestor({ node: nodeStub, treeMap: treeMapMock })
        expect(ancestorId).to.be.equals(expectAncestorNodeId)
    })

    it(`Should update node at depth level when updateNode is called`, () => {
        let objToBeUpdated = {
            root_node: {
                node_child: {
                    grand_child: 'grand_child value',
                    grand_child_1: 'grand_child_1 value',
                },
                node_child_1: 'node_child_1 value',
            },
        }
        const expectResult = {
            root_node: {
                node_child: {
                    grand_child: 'grand_child value',
                    grand_child_1: 'new grand_child_1 value',
                },
                node_child_1: 'node_child_1 value',
            },
        }
        helper.updateNode({
            obj: objToBeUpdated,
            node: {
                id: 'grand_child_1',
                value: 'new grand_child_1 value',
            },
        })
        expect(objToBeUpdated).to.be.deep.equals(expectResult)
    })

    describe('objToTree and treeToObj assertions', () => {
        it(`Should convert object to tree array accurately when objToTree is called`, () => {
            const treeArr = helper.objToTree({
                obj: dummyTree,
                keepPrimitiveValue: true,
                level: 0,
            })

            expect(treeArr).to.be.deep.equals(treeArrStub)
        })

        it(`Should convert changed nodes to an object when treeToObj is called`, () => {
            const changedNodes = [
                {
                    nodeId: 4,
                    parentNodeId: 1,
                    level: 1,
                    id: 'node_child_1',
                    value: 'new node_child_1 value',
                    originalValue: 'node_child_1 value',
                    leaf: true,
                },
                {
                    nodeId: 3,
                    parentNodeId: 2,
                    level: 2,
                    id: 'grand_child',
                    value: 'new grand_child value',
                    originalValue: 'grand_child value',
                    leaf: true,
                },
            ]

            const expectReturn = {
                root_node: {
                    node_child: { grand_child: 'new grand_child value' },
                    node_child_1: 'new node_child_1 value',
                },
            }

            const resultObj = helper.treeToObj({
                changedNodes,
                tree: treeArrStub,
            })

            expect(resultObj).to.be.deep.equals(expectReturn)
        })
    })

    describe('convertType assertions', () => {
        for (const [key, value] of Object.entries(dummyValues)) {
            let expectResult = value
            let des = `Should return ${expectResult} when value is ${key}`
            switch (value) {
                case undefined:
                    expectResult = 'undefined'
                    des = des.replace(`return ${expectResult}`, `return ${expectResult} as string`)
                    break
                case null:
                    expectResult = 'null'
                    des = des.replace(`return ${expectResult}`, `return ${expectResult} as string`)
                    break
                default:
                    des = des.replace(`return ${expectResult}`, `not change value type`)
            }
            it(des, () => {
                expect(helper.convertType(value)).to.be.equals(expectResult)
            })
        }
    })

    it('capitalizeFirstLetter should return new string with first letter capitalized', () => {
        const str = 'server'
        expect(helper.capitalizeFirstLetter(str)).to.be.equals('Server')
    })

    describe('IEC convertSize assertions', () => {
        const bytes = 1099511628000
        const IECSuffixes = [undefined, 'Ki', 'Mi', 'Gi', 'Ti']
        const expectReturnsIEC = [bytes, 1073741824, 1048576, 1024, 1]
        IECSuffixes.forEach((suffix, i) => {
            let des = `Should convert and return accurate value if suffix is ${suffix}`
            let reverse = false
            switch (suffix) {
                case undefined:
                    des = `Should return same value if suffix is undefined`
                    break
                default:
                    reverse = true
            }
            it(des, () => {
                expect(
                    helper.convertSize({ suffix, val: bytes, isIEC: true, reverse })
                ).to.be.equals(expectReturnsIEC[i])
            })
        })
    })

    describe('SI convertSize assertions', () => {
        const bits = 1000000000000
        const SISuffixes = [undefined, 'k', 'M', 'G', 'T']
        const expectReturnsSI = [bits, 1000000000, 1000000, 1000, 1]
        SISuffixes.forEach((suffix, i) => {
            let des = `Should convert and return accurate value if suffix is ${suffix}`
            let reverse = false
            switch (suffix) {
                case undefined:
                    des = `Should return same value if suffix is undefined`
                    break
                default:
                    reverse = true
            }
            it(des, () => {
                expect(
                    helper.convertSize({ suffix, val: bits, isIEC: false, reverse })
                ).to.be.equals(expectReturnsSI[i])
            })
        })
    })

    const durationSuffixes = ['ms', 's', 'm', 'h']

    describe('convertDuration assertions when base value is ms', () => {
        const ms = 3600000
        const expectReturns = [3600000, 3600, 60, 1]
        durationSuffixes.forEach((suffix, i) => {
            let des = `Should convert ${ms}ms to ${expectReturns[i]}${suffix} `
            it(des, () => {
                expect(
                    helper.convertDuration({ suffix, val: ms, toMilliseconds: false })
                ).to.be.equals(expectReturns[i])
            })
        })
    })

    describe('convertDuration assertions with toMilliseconds mode enables', () => {
        const values = [3600000, 3600, 60, 1]
        const expectReturns = 3600000
        durationSuffixes.forEach((suffix, i) => {
            let des = `Should convert ${values[i]}${suffix} to ${expectReturns}ms`
            it(des, () => {
                expect(
                    helper.convertDuration({
                        suffix,
                        val: values[i],
                        toMilliseconds: true,
                    })
                ).to.be.equals(expectReturns)
            })
        })
    })

    it('getSuffixFromValue should return object with suffix and indexOfSuffix keys', () => {
        const paramObj = { value: '1000ms' }
        let suffixes = ['ms', 's', 'm', 'h']
        const result = helper.getSuffixFromValue(paramObj, suffixes)
        expect(result).to.have.all.keys('suffix', 'indexOfSuffix')
        expect(result.suffix).to.be.equals('ms')
        expect(result.indexOfSuffix).to.be.equals(4)
    })

    describe('genLineDataSet assertions', () => {
        const label = 'line 0'
        const value = 150
        const colorIndex = 0
        it('Should return dataset object with accurate keys', () => {
            const result = helper.genLineDataSet({ label, value, colorIndex })
            expect(result).to.have.all.keys(
                'label',
                'id',
                'type',
                'backgroundColor',
                'borderColor',
                'borderWidth',
                'lineTension',
                'data'
            )
        })
        it(`Should get timestamp form Date.now() if timestamp
        argument is not provided`, () => {
            const result = helper.genLineDataSet({ label, value, colorIndex })
            expect(result.data.length).to.be.equals(1)
            expect(result.data[0].x).to.be.a('number')
        })
        it(`Should use provided timestamp argument`, () => {
            const timestamp = Date.now()
            const result = helper.genLineDataSet({ label, value, colorIndex, timestamp })
            expect(result.data.length).to.be.equals(1)
            expect(result.data[0].x).to.be.equals(timestamp)
        })
        it(`Should have resourceId key if id argument is provided`, () => {
            const id = 'server_0'
            const result = helper.genLineDataSet({ label, value, colorIndex, id })
            expect(result).to.have.property('resourceId', id)
        })
        it(`Should create data array for key data if
        data argument is not provided`, () => {
            const result = helper.genLineDataSet({ label, value, colorIndex })
            expect(result.data[0]).to.have.all.keys('x', 'y')
        })
        it(`Should use data argument for key data`, () => {
            const data = [
                { x: 1598972034170, y: value - 10 },
                { x: 1600000000000, y: value },
            ]
            const result = helper.genLineDataSet({ label, value, colorIndex, data })
            expect(result.data).to.be.deep.equals(data)
        })
    })
})
