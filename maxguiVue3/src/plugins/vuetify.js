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
import 'vuetify/styles'
import { createVuetify } from 'vuetify'

export const vuetifyMxsTheme = {
  dark: false,
  colors: {
    background: '#FFFFFF',
    surface: '#FFFFFF',
    'surface-bright': '#FFFFFF',
    'surface-light': '#EEEEEE',
    'surface-variant': '#424242',
    'on-surface-variant': '#EEEEEE',
    primary: '#0e9bc0',
    'primary-darken-1': '#1F5592',
    secondary: '#E6EEF1',
    'secondary-darken-1': '#018786',
    error: '#eb5757',
    info: '#2d9cdb',
    success: '#7dd012',
    warning: '#f59d34',
    accent: '#2f99a3',
    anchor: '#2d9cdb',
    navigation: '#424F62',
    ['deep-ocean']: '#003545',
    ['blue-azure']: '#0e6488',
    ['grayed-out']: '#a6a4a6',
  },
  variables: {
    'border-color': '#000000',
    'border-opacity': 0.12,
    'high-emphasis-opacity': 0.87,
    'medium-emphasis-opacity': 0.6,
    'disabled-opacity': 0.38,
    'idle-opacity': 0.04,
    'hover-opacity': 0.04,
    'focus-opacity': 0.12,
    'selected-opacity': 0.08,
    'activated-opacity': 0.12,
    'pressed-opacity': 0.12,
    'dragged-opacity': 0.08,
    'theme-kbd': '#212529',
    'theme-on-kbd': '#FFFFFF',
    'theme-code': '#F5F5F5',
    'theme-on-code': '#000000',
  },
}

export default createVuetify({
  theme: {
    defaultTheme: 'vuetifyMxsTheme',
    themes: {
      vuetifyMxsTheme,
    },
  },
})
