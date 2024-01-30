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
import * as dataTableHelpers from '@src/utils/dataTableHelpers'
import * as mockData from '@src/utils/mockData'

describe('dataTableHelpers', () => {
    describe('objToTree and treeToObj assertions', () => {
        it(`Should convert object to tree array accurately when objToTree is called`, () => {
            const treeArr = dataTableHelpers.objToTree({
                obj: mockData.nestedObj,
                keepPrimitiveValue: true,
                level: 0,
            })

            expect(treeArr).to.be.deep.equals(mockData.treeNodes)
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

            const resultObj = dataTableHelpers.treeToObj({
                changedNodes,
                tree: mockData.treeNodes,
            })

            expect(resultObj).to.be.deep.equals(expectReturn)
        })
    })

    it('getSuffixFromValue should return object with suffix and indexOfSuffix keys', () => {
        const paramObj = { value: '1000ms' }
        let suffixes = ['ms', 's', 'm', 'h']
        const result = dataTableHelpers.getSuffixFromValue(paramObj, suffixes)
        expect(result).to.have.all.keys('suffix', 'indexOfSuffix')
        expect(result.suffix).to.be.equals('ms')
        expect(result.indexOfSuffix).to.be.equals(4)
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
                    dataTableHelpers.convertSize({ suffix, val: bytes, isIEC: true, reverse })
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
                    dataTableHelpers.convertSize({ suffix, val: bits, isIEC: false, reverse })
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
                    dataTableHelpers.convertDuration({ suffix, val: ms, toMilliseconds: false })
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
                    dataTableHelpers.convertDuration({
                        suffix,
                        val: values[i],
                        toMilliseconds: true,
                    })
                ).to.be.equals(expectReturns)
            })
        })
    })
})
