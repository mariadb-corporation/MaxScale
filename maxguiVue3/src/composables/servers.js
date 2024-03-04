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
import { SERVER_OP_TYPES, MXS_OBJ_TYPES } from '@/constants'

/**
 * @param {object} currState - computed property
 * @returns {object}
 */
export function useServerOpMap(currState) {
  const { MAINTAIN, CLEAR, DRAIN, DELETE } = SERVER_OP_TYPES
  const { t } = useI18n()
  const { deleteObj } = useMxsObjActions(MXS_OBJ_TYPES.SERVERS)
  const goBack = useGoBack()
  const store = useStore()
  const currStateMode = computed(() => {
    let currentState = currState.value.toLowerCase()
    if (currentState.indexOf(',') > 0)
      currentState = currentState.slice(0, currentState.indexOf(','))
    return currentState
  })
  return {
    computedMap: computed(() => ({
      [MAINTAIN]: {
        title: t('serverOps.actions.maintain'),
        type: MAINTAIN,
        icon: 'mxs:maintenance',
        iconSize: 22,
        color: 'primary',
        info: t(`serverOps.info.maintain`),
        params: 'set?state=maintenance',
        disabled: currStateMode.value === 'maintenance',
        saveText: 'set',
      },
      [CLEAR]: {
        title: t('serverOps.actions.clear'),
        type: CLEAR,
        icon: 'mxs:restart',
        iconSize: 22,
        color: 'primary',
        info: '',
        params: `clear?state=${currStateMode.value === 'drained' ? 'drain' : currStateMode.value}`,
        disabled: currStateMode.value !== 'maintenance' && currStateMode.value !== 'drained',
      },
      [DRAIN]: {
        title: t('serverOps.actions.drain'),
        type: DRAIN,
        icon: 'mxs:drain',
        iconSize: 22,
        color: 'primary',
        info: t(`serverOps.info.drain`),
        params: `set?state=drain`,
        disabled: currStateMode.value === 'maintenance' || currStateMode.value === 'drained',
      },
      [DELETE]: {
        title: t('serverOps.actions.delete'),
        type: DELETE,
        icon: 'mxs:delete',
        iconSize: 18,
        color: 'error',
        info: '',
        disabled: false,
      },
    })),
    handler: async ({ op, id, forceClosing, callback }) => {
      switch (op.type) {
        case DELETE:
          await deleteObj(id)
          goBack()
          break
        case DRAIN:
        case CLEAR:
        case MAINTAIN:
          await store.dispatch('servers/setOrClearServerState', {
            id,
            opParams: op.params,
            type: op.type,
            callback,
            forceClosing: forceClosing,
          })
          break
      }
    },
  }
}
