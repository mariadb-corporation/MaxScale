/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { MXS_OBJ_TYPE_MAP } from '@/constants'

const { SERVICES, SERVERS, MONITORS, LISTENERS } = MXS_OBJ_TYPE_MAP

export const ICON_SHEETS = {
  [MONITORS]: {
    frames: ['mxs:stopped', 'mxs:good'],
    colorClasses: ['text-grayed-out', 'text-success'],
  },
  [SERVICES]: {
    frames: ['mxs:critical', 'mxs:good', 'mxs:stopped'],
    colorClasses: ['text-error', 'text-success', 'text-grayed-out'],
  },
  [LISTENERS]: {
    frames: ['mxs:critical', 'mxs:good', 'mxs:stopped'],
    colorClasses: ['text-error', 'text-success', 'text-grayed-out'],
  },
  [SERVERS]: {
    frames: ['mxs:criticalServer', 'mxs:goodServer', 'mxs:maintenance'],
    colorClasses: ['text-error', 'text-success', 'text-grayed-out'],
  },
  replication: {
    frames: ['mxs:stopped', 'mxs:good', 'mxs:statusWarning'],
    colorClasses: ['text-grayed-out', 'text-success', 'text-warning'],
  },
  log: {
    frames: [
      'mxs:alertWarning',
      'mxs:critical',
      'mxs:statusInfo',
      'mxs:reports',
      'mxs:statusInfo',
      '$mdiBug',
    ],
    colorClasses: [
      'text-error',
      'text-error',
      'text-warning',
      'text-info',
      'text-info',
      'text-accent',
    ],
  },
  config_sync: {
    frames: ['$mdiSyncAlert', '$mdiCogSyncOutline', '$mdiCogSyncOutline'],
    colorClasses: ['text-error', 'text-success', 'text-primary'],
  },
}

const LOG_PRIORITY_FRAME_IDX_MAP = {
  alert: 0,
  error: 1,
  warning: 2,
  notice: 3,
  info: 4,
  debug: 5,
}
/**
 * @param {string} type - sheet type
 * @param {string} value - value
 * @returns {number} - frame index
 */
export function getFrameIdx(type, value) {
  switch (type) {
    case SERVICES:
      if (value.includes('Started')) return 1
      if (value.includes('Stopped')) return 2
      if (value.includes('Allocated') || value.includes('Failed')) return 0
      return -1
    case SERVERS:
      if (value.includes('Maintenance')) return 2
      else if (value === 'Running' || value.includes('Down')) return 0
      else if (value.includes('Running')) return 1
      return -1
    case MONITORS:
      if (value.includes('Running')) return 1
      if (value.includes('Stopped')) return 0
      return -1
    case LISTENERS:
      if (value === 'Running') return 1
      else if (value === 'Stopped') return 2
      else if (value === 'Failed') return 0
      return -1
    case 'replication':
      if (value === 'Stopped') return 0
      // healthy icon
      else if (value === 'Running') return 1
      return 2
    case 'log': {
      const frameIdx = LOG_PRIORITY_FRAME_IDX_MAP[value]
      if (frameIdx >= 0) return frameIdx
      return -1
    }
    case 'config_sync':
      if (value === 'No configuration changes') return 2
      if (value === 'OK') return 1
      return 0
  }
}
