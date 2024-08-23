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
import { http } from '@/utils/axios'
import { t as typy } from 'typy'
import { tryAsync } from '@/utils/helpers'
import { SERVICE_OP_TYPE_MAP, MXS_OBJ_TYPE_MAP, SNACKBAR_TYPE_MAP } from '@/constants'

/**
 * @param {object} currState - computed property
 * @returns {object}
 */
export function useOpMap(currState) {
  const { STOP, START, DESTROY } = SERVICE_OP_TYPE_MAP
  const { t } = useI18n()
  const { deleteObj } = useMxsObjActions(MXS_OBJ_TYPE_MAP.SERVICES)
  const goBack = useGoBack()
  const store = useStore()
  return {
    computedMap: computed(() => ({
      [STOP]: {
        title: t('serviceOps.actions.stop'),
        type: STOP,
        icon: 'mxs:stopped',
        iconSize: 22,
        color: 'primary',
        params: 'stop',
        disabled: currState.value === 'Stopped',
      },
      [START]: {
        title: t('serviceOps.actions.start'),
        type: START,
        icon: 'mxs:running',
        iconSize: 22,
        color: 'primary',
        params: 'start',
        disabled: currState.value === 'Started',
      },
      [DESTROY]: {
        title: t('serviceOps.actions.destroy'),
        type: DESTROY,
        icon: 'mxs:delete',
        iconSize: 18,
        color: 'error',
        disabled: false,
      },
    })),
    handler: async ({ op, id, callback }) => {
      switch (op.type) {
        case DESTROY:
          await deleteObj(id)
          goBack()
          break
        case STOP:
        case START: {
          const [, res] = await tryAsync(http.put(`/services/${id}/${op.params}`))
          if (res.status === 204) {
            store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
              text: [`Service ${id} is ${op.params === 'start' ? 'started' : 'stopped'}`],
              type: SNACKBAR_TYPE_MAP.SUCCESS,
            })
            await typy(callback).safeFunction()
          }
          break
        }
      }
    },
  }
}
