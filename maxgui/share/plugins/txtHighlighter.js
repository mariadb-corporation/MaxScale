/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { lodash } from '@share/utils/helpers'
/**
 * Create a span tag with highlighted text
 * @param {String} txt - text to have highlighted text
 * @param {String} highlightTxt - highlight txt
 * @returns {String} Return highlighted tag
 */
function highlight({ txt, highlightTxt }) {
    let res = txt
    if (highlightTxt !== '') {
        const regex = new RegExp('(' + lodash.escapeRegExp(highlightTxt) + ')', 'gi')
        res = res.replace(regex, '<span class="mxs-txt-highlight">$&</span>')
    }
    return res
}
/**
 * This directive accepts 1 value argument and uses that value to find and highlight that value on
 * the text element bound with this directive.
 * Usage example: Place this directive v-mxs-highlighter="search_keyword" on the element
 * that renders text.
 */
export default {
    install: Vue => {
        Vue.directive('mxs-highlighter', {
            bind(el, binding) {
                const highlightTxt = binding.value
                const txt = el.innerHTML
                el.innerHTML = highlight({ txt, highlightTxt })
            },
            componentUpdated(el, binding, vnode) {
                const highlightTxt = binding.value
                let txt = ''
                if (vnode.elm && vnode.elm.innerText) txt = vnode.elm.innerText
                el.innerHTML = highlight({ txt, highlightTxt })
            },
        })
    },
}
