/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
const CANCEL_MESSAGE = 'canceled'
/**
 * Default handler for error response status codes
 */
export async function defErrStatusHandler({ store, error }) {
    const { getErrorsArr, delay } = store.vue.$helpers
    store.commit(
        'mxsApp/SET_SNACK_BAR_MESSAGE',
        {
            text: getErrorsArr(error),
            type: 'error',
        },
        { root: true }
    )
    /* When request is dispatched in a modal, an overlay_type loading will be set,
     * Turn it off before returning error
     */
    if (store.state.mxsApp.overlay_type !== null)
        await delay(600).then(() => store.commit('mxsApp/SET_OVERLAY_TYPE', null, { root: true }))
}

export function isCancelled(error) {
    return error.toString().includes(CANCEL_MESSAGE)
}

export function handleNullStatusCode({ store, error }) {
    if (isCancelled(error))
        // request is cancelled by user, so no response is received
        store.vue.$logger.info(error.toString())
    else
        store.commit(
            'mxsApp/SET_SNACK_BAR_MESSAGE',
            {
                text: ['Lost connection to MaxScale, please check if MaxScale is running'],
                type: 'error',
            },
            { root: true }
        )
}
