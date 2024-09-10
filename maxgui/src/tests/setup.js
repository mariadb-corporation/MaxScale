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
import { useI18n } from 'vue-i18n'

// Required for Vuetify as modal component are mounted to mxs-app
const el = document.createElement('div')
el.setAttribute('id', 'mxs-app')
document.body.appendChild(el)

global.Worker = vi.fn(() => ({ postMessage: vi.fn(), onmessage: vi.fn(), terminate: vi.fn() }))

global.ResizeObserver = require('resize-observer-polyfill')
global.WebSocket = vi.fn()
global.localStorage = {
  getItem: vi.fn(),
  setItem: vi.fn(),
  removeItem: vi.fn(),
  clear: vi.fn(),
}

vi.mock('localforage', () => ({
  default: {
    getItem: vi.fn().mockResolvedValue(null),
    setItem: vi.fn().mockResolvedValue(null),
    removeItem: vi.fn().mockResolvedValue(null),
    clear: vi.fn().mockResolvedValue(null),
  },
}))

vi.mock('vue-i18n')
useI18n.mockReturnValue({ t: (tKey) => tKey, tm: (tKey) => tKey })
vi.mock('axios', () => ({
  default: {
    post: vi.fn(() => Promise.resolve(null)),
    get: vi.fn(() => Promise.resolve(null)),
    delete: vi.fn(() => Promise.resolve(null)),
    put: vi.fn(() => Promise.resolve(null)),
    create: vi.fn().mockReturnThis(),
    interceptors: {
      request: { use: vi.fn().mockReturnThis(), eject: vi.fn().mockReturnThis() },
      response: { use: vi.fn().mockReturnThis(), eject: vi.fn().mockReturnThis() },
    },
  },
}))
// mock monaco
vi.mock('@/components/common/SqlEditor/customMonaco.js', () => ({ default: {} }))
