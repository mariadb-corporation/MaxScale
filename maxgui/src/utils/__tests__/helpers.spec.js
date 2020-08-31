/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
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
})
