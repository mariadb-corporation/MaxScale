import Vue from 'vue'
import { t } from 'typy'

Object.defineProperties(Vue.prototype, {
    $typy: {
        get() {
            return t
        },
    },
})
