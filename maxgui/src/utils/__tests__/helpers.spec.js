/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import * as maxguiHelpers from '@rootSrc/utils/helpers'
import * as mockData from '@rootSrc/utils/mockData'

describe('maxgui helpers unit tests', () => {
    describe('stringifyNullOrUndefined assertions', () => {
        for (const [key, value] of Object.entries(mockData.mixedTypeValues)) {
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
                expect(maxguiHelpers.stringifyNullOrUndefined(value)).to.be.equals(expectResult)
            })
        }
    })

    describe('genLineStreamDataset assertions', () => {
        const label = 'line 0'
        const value = 150
        const colorIndex = 0
        it('Should return dataset object with accurate keys', () => {
            const result = maxguiHelpers.genLineStreamDataset({ label, value, colorIndex })
            expect(result).to.have.all.keys(
                'label',
                'id',
                'type',
                'backgroundColor',
                'borderColor',
                'borderWidth',
                'data',
                'fill'
            )
        })
        it(`Should get timestamp form Date.now() if timestamp
        argument is not provided`, () => {
            const result = maxguiHelpers.genLineStreamDataset({ label, value, colorIndex })
            expect(result.data.length).to.be.equals(1)
            expect(result.data[0].x).to.be.a('number')
        })
        it(`Should use provided timestamp argument`, () => {
            const timestamp = Date.now()
            const result = maxguiHelpers.genLineStreamDataset({
                label,
                value,
                colorIndex,
                timestamp,
            })
            expect(result.data.length).to.be.equals(1)
            expect(result.data[0].x).to.be.equals(timestamp)
        })
        it(`Should have resourceId key if id argument is provided`, () => {
            const id = 'server_0'
            const result = maxguiHelpers.genLineStreamDataset({ label, value, colorIndex, id })
            expect(result).to.have.property('resourceId', id)
        })
        it(`Should create data array for key data if
        data argument is not provided`, () => {
            const result = maxguiHelpers.genLineStreamDataset({ label, value, colorIndex })
            expect(result.data[0]).to.have.all.keys('x', 'y')
        })
        it(`Should use data argument for key data`, () => {
            const data = [
                { x: 1598972034170, y: value - 10 },
                { x: 1600000000000, y: value },
            ]
            const result = maxguiHelpers.genLineStreamDataset({ label, value, colorIndex, data })
            expect(result.data).to.be.deep.equals(data)
        })
    })
})
