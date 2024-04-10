/*
 * Copyright (c) 2023 MariaDB plc
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
import statusIconHelpers from '@src/utils/statusIconHelpers'
import { MXS_OBJ_TYPES } from '@share/constants'

const { SERVICES, SERVERS, MONITORS, LISTENERS } = MXS_OBJ_TYPES

describe('statusIconHelpers tests', () => {
    describe('service state icon assertions', () => {
        const dummyServiceStates = ['Started', 'Stopped', 'Allocated', 'Failed', 'Unknown']
        const expectedReturn = [1, 2, 0, 0, -1]
        dummyServiceStates.forEach((state, i) => {
            let des = `Should return ${expectedReturn[i]} when state is ${state}`
            if (state === 'Unknown')
                des = des.replace(`Should return ${expectedReturn[i]}`, 'Should return -1')
            it(des, () => {
                expect(statusIconHelpers[SERVICES](state)).to.be.equals(expectedReturn[i])
            })
        })
    })

    describe('server state icon assertions', () => {
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
                expect(statusIconHelpers[SERVERS](state)).to.be.equals(expectedReturn[i])
            })
        })
    })

    describe('monitor state icon assertions', () => {
        const dummyServiceStates = ['Running', 'Stopped', 'Unknown']
        const expectedReturn = [1, 0, -1]
        dummyServiceStates.forEach((state, i) => {
            let des = `Should return ${expectedReturn[i]} when state is ${state}`
            if (state === 'Unknown')
                des = des.replace(`Should return ${expectedReturn[i]}`, 'Should return -1')
            it(des, () => {
                expect(statusIconHelpers[MONITORS](state)).to.be.equals(expectedReturn[i])
            })
        })
    })

    describe('listener state icon assertions', () => {
        const dummyServiceStates = ['Failed', 'Running', 'Stopped', 'Unknown']
        const expectedReturn = [0, 1, 2, -1]
        dummyServiceStates.forEach((state, i) => {
            let des = `Should return ${expectedReturn[i]} when state is ${state}`
            if (state === 'Unknown')
                des = des.replace(`Should return ${expectedReturn[i]}`, 'Should return -1')
            it(des, () => {
                expect(statusIconHelpers[LISTENERS](state)).to.be.equals(expectedReturn[i])
            })
        })
    })
})
