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
import '@mdi/font/css/materialdesignicons.css'
import 'vuetify/styles'
import { createVuetify } from 'vuetify'
import { aliases, mdi } from 'vuetify/iconsets/mdi-svg'
import icons from '@/assets/icons'

export const colors = {
  primary: '#0e9bc0',
  secondary: '#e6eef1',
  accent: '#2f99a3',
  anchor: '#2d9cdb',
  text: '#4f5051',
  'small-text': '#6c7c7b',
  'text-subtle': '#a3bac0',
  navigation: '#424f62',
  'code-color': '#525a65',
  success: '#7dd012',
  error: '#eb5757',
  warning: '#f59d34',
  info: '#2d9cdb',
  alert: '#eb5757',
  notice: '#525a65',
  debug: '#2f99a3',
  separator: '#e8eef1',
  'grayed-out': '#a6a4a6',
  'table-border': '#e7eef1',
  'tr-hovered-color': '#f2fcff',
  'selected-tr-color': '#e3f5fb',
  'deep-ocean': '#003545',
  'blue-azure': '#0e6488',
  'electric-ele': '#abc74a',
}

const vuetifyMxsTheme = {
  dark: false,
  colors: {
    background: '#ffffff',
    surface: '#ffffff',
    'surface-bright': '#ffffff',
    'surface-light': '#eeeeee',
    'surface-variant': '#424242',
    'on-surface-variant': '#eeeeee',
    'primary-darken-1': '#0b7490',
    'secondary-darken-1': '#aac5cf',
    ...colors,
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
    'theme-on-kbd': '#ffffff',
    'theme-code': '#F5F5F5',
    'theme-on-code': '#000000',
  },
}

const commonProps = {
  density: 'comfortable',
  color: colors.primary,
  baseColor: colors['text-subtle'],
  class: ['text-navigation'],
}

export default createVuetify({
  icons: {
    aliases,
    sets: { mdi, mxs: icons },
  },
  theme: {
    defaultTheme: 'vuetifyMxsTheme',
    themes: {
      vuetifyMxsTheme,
    },
  },
  defaults: {
    VOverlay: { class: 'v-mxs--style' },
    VTextField: { variant: 'outlined', ...commonProps },
    VSelect: {
      variant: 'outlined',
      ...commonProps,
      clearIcon: '$close',
      menuProps: {
        contentClass: [...commonProps.class, 'v-select--menu-mariadb'],
        location: 'bottom',
      },
    },
    VCheckbox: commonProps,
    VTextarea: { variant: 'outlined', bgColor: colors.background, ...commonProps },
    VDataTable: {
      density: commonProps.density,
      fixedHeader: true,
    },
  },
})
