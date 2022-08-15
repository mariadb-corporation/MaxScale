const CANCEL_MESSAGE = 'canceled'
/**
 * Default handler for error response status codes
 */
export async function defErrStatusHandler({ store, error }) {
    const { getErrorsArr, delay } = store.vue.$help
    store.commit('SET_SNACK_BAR_MESSAGE', {
        text: getErrorsArr(error),
        type: 'error',
    })
    /* When request is dispatched in a modal, an overlay_type loading will be set,
     * Turn it off before returning error
     */
    if (store.state.overlay_type !== null)
        await delay(600).then(() => store.commit('SET_OVERLAY_TYPE', null))
    return Promise.reject(error)
}

export function handleNullStatusCode({ store, error }) {
    if (error.toString().includes(CANCEL_MESSAGE))
        // request is cancelled by user, so no response is received
        return store.vue.$logger('handleNullStatusCode').info(error.toString())
    else
        return store.commit('SET_SNACK_BAR_MESSAGE', {
            text: ['Lost connection to MaxScale, please check if MaxScale is running'],
            type: 'error',
        })
}
