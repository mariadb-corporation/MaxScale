/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import * as commonHelpers from '@share/utils/helpers'

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
                expect(
                    commonHelpers.dateFormat({
                        moment: Vue.moment,
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
})
