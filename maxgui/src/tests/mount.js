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
import { mount } from '@vue/test-utils'
import PortalVue from 'portal-vue'
import { lodash } from '@/utils/helpers'
import typy from '@/plugins/typy'
import helpers from '@/plugins/helpers'
import logger from '@/plugins/logger'
import vuetify from '@/plugins/vuetify'
import txtHighlighter from '@/plugins/txtHighlighter'
import resizeObserver from '@/plugins/resizeObserver'
import router from '@/router'
import store from '@/store'
import { WS_KEY, WS_EDITOR_KEY, TBL_STRUCTURE_EDITOR_KEY } from '@/constants/injectionKeys'

export default (component, options, mockStore) => {
  const mountOpts = lodash.merge(
    {
      shallow: true,
      global: {
        plugins: [
          typy,
          helpers,
          logger,
          router,
          vuetify,
          PortalVue,
          mockStore ? mockStore : store,
          txtHighlighter,
          resizeObserver,
        ],
        mocks: {
          $t: (tKey) => tKey,
          $tm: (tKey) => tKey,
        },
        stubs: { 'i18n-t': true },
      },
    },
    options
  )
  mountOpts.global.provide = {
    [WS_KEY]: [],
    [WS_EDITOR_KEY]: [],
    [TBL_STRUCTURE_EDITOR_KEY]: [],
  }
  return mount(component, mountOpts)
}
