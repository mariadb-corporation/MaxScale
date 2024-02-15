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
import { mount } from '@vue/test-utils'
import PortalVue from 'portal-vue'
import commonComponents from '@/components/common'
import { lodash } from '@/utils/helpers'
import i18n from '@/plugins/i18n'
import typy from '@/plugins/typy'
import helpers from '@/plugins/helpers'
import logger from '@/plugins/logger'
import shortkey from '@/plugins/shortkey'
import vuetify from '@/plugins/vuetify'
import axios from '@/plugins/axios'

global.ResizeObserver = require('resize-observer-polyfill')

export default (component, options) => {
  return mount(
    component,
    lodash.mergeWith(
      {
        global: {
          plugins: [i18n, typy, helpers, logger, shortkey, vuetify, axios, PortalVue],
          components: commonComponents,
        },
      },
      options,
      (objValue, srcValue) => {
        if (lodash.isArray(objValue)) return [...objValue, ...srcValue]
      }
    )
  )
}
