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
import * as commonHelpers from '@share/utils/helpers'
import * as mockData from '@src/utils/mockData'

describe('common helpers unit tests', () => {
    it('strReplaceAt should return new string accurately', () => {
        expect(commonHelpers.strReplaceAt({ str: 'Servers', index: 6, newChar: '' })).to.be.equals(
            'Server'
        )
        expect(
            commonHelpers.strReplaceAt({ str: 'rgba(171,199,74,1)', index: 16, newChar: '0.1' })
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
            let result = commonHelpers.getErrorsArr(error)
            expect(result).to.be.an('array')
            expect(result).to.be.deep.equals(expectedReturn[i])
        })
    })

    describe('dateFormat assertions', () => {
        const dummyValue = 'Tue, 01 Sep 2020 05:55:56 GMT'
        const dummyFormatTypes = [undefined, 'HH:mm:ss']
        const expectedReturn = ['Tue, 01 Sep 2020 08:55:56', '08:55:56']
        dummyFormatTypes.forEach((type, i) => {
            let des = `Should return date string with format ${type}`
            if (type === undefined)
                des = des.replace('default format E, dd MMM yyyy HH:mm:ss', `format ${type}`)
            it(des, () => {
                expect(
                    commonHelpers.dateFormat({
                        value: dummyValue,
                        formatType: type,
                    })
                ).to.be.equals(expectedReturn[i])
            })
        })
    })

    it('capitalizeFirstLetter should return new string with first letter capitalized', () => {
        const str = 'server'
        expect(commonHelpers.capitalizeFirstLetter(str)).to.be.equals('Server')
    })

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
                expect(commonHelpers.stringifyNullOrUndefined(value)).to.be.equals(expectResult)
            })
        }
    })

    describe('genLineStreamDataset assertions', () => {
        const label = 'line 0'
        const value = 150
        const colorIndex = 0
        it('Should return dataset object with accurate keys', () => {
            const result = commonHelpers.genLineStreamDataset({ label, value, colorIndex })
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
            const result = commonHelpers.genLineStreamDataset({ label, value, colorIndex })
            expect(result.data.length).to.be.equals(1)
            expect(result.data[0].x).to.be.a('number')
        })
        it(`Should use provided timestamp argument`, () => {
            const timestamp = Date.now()
            const result = commonHelpers.genLineStreamDataset({
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
            const result = commonHelpers.genLineStreamDataset({ label, value, colorIndex, id })
            expect(result).to.have.property('resourceId', id)
        })
        it(`Should create data array for key data if
        data argument is not provided`, () => {
            const result = commonHelpers.genLineStreamDataset({ label, value, colorIndex })
            expect(result.data[0]).to.have.all.keys('x', 'y')
        })
        it(`Should use data argument for key data`, () => {
            const data = [
                { x: 1598972034170, y: value - 10 },
                { x: 1600000000000, y: value },
            ]
            const result = commonHelpers.genLineStreamDataset({ label, value, colorIndex, data })
            expect(result.data).to.be.deep.equals(data)
        })
    })
})
