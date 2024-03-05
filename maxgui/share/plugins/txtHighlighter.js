/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { lodash } from '@share/utils/helpers'
import { t } from 'typy'

/**
 * Create a span tag with highlighted text
 * @param {String} txt - text to have highlighted text
 * @param {String} keyword - highlight txt
 * @returns {String} Return highlighted tag
 */
function highlight(binding) {
    const { keyword, txt } = binding.value
    let res = `${txt}`
    if (keyword !== '') {
        const regex = new RegExp('(' + lodash.escapeRegExp(keyword) + ')', 'gi')
        res = res.replace(regex, '<span class="mxs-txt-highlight">$&</span>')
    }
    return res
}
function updateTxt(el, binding) {
    if (t(binding, 'value.keyword').isDefined && t(binding, 'value.txt').isDefined)
        el.innerHTML = highlight(binding)
}
/**
 * Usage example: Place this directive v-mxs-highlighter="{ keyword, txt}" on the element
 * that renders text.
 */
export default {
    install: Vue => {
        Vue.directive('mxs-highlighter', {
            bind(...args) {
                updateTxt(...args)
            },
            componentUpdated(...args) {
                updateTxt(...args)
            },
            unbind(...args) {
                updateTxt(...args)
            },
        })
    },
}
