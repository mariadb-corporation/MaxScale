/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import usersService from '@/services/usersService'
import { PERSIST_TOKEN_OPT } from '@/constants'

const authHttpMock = vi.hoisted(() => ({ get: vi.fn(() => Promise.resolve({ status: 204 })) }))
const routerPushMock = vi.hoisted(() => vi.fn())
const stubRedirectPath = vi.hoisted(() => '/dashboard/servers')

describe('login tests', () => {
  vi.mock('@/router', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      push: routerPushMock,
      currentRoute: {
        value: { query: { redirect: stubRedirectPath } },
      },
    },
  }))
  vi.mock('@/utils/axios', async (importOriginal) => ({
    ...(await importOriginal()),
    authHttp: authHttpMock,
    http: { get: vi.fn(() => Promise.resolve(null)) },
  }))
  vi.mock('@/services/maxscaleService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      fetchVersion: vi.fn(() => Promise.resolve(null)),
    },
  }))

  afterEach(() => vi.clearAllMocks())

  const rememberMeTestCases = [true, false]
  rememberMeTestCases.forEach((rememberMe) =>
    it('Should send log request with expected params', async () => {
      await usersService.login({ rememberMe, auth: { username: 'admin', password: 'mariadb' } })
      expect(authHttpMock.get.mock.calls[0][0]).toBe(
        rememberMe ? `/auth?${PERSIST_TOKEN_OPT}` : '/auth?persist=yes'
      )
    })
  )

  it('Should navigate to dashboard page once authenticating process is succeed', async () => {
    await usersService.login({
      rememberMe: false,
      auth: { username: 'admin', password: 'mariadb' },
    })
    expect(routerPushMock.mock.calls[0][0]).toBe(stubRedirectPath)
  })
})
//TODO: Add more tests
