import { t } from 'typy'

export default {
    install: Vue => {
        Vue.prototype.$typy = t
    },
}
