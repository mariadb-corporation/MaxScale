import Vue from 'vue'
import Vuetify from 'vuetify/lib'
import helpers from '@share/plugins/helpers'
import logger from '@share/plugins/logger'
import typy from '@share/plugins/typy'
import shortkey from '@share/plugins/shortkey'
import Vuex from 'vuex'
import PortalVue from 'portal-vue'
import VueMoment from 'vue-moment'
import momentDurationFormatSetup from 'moment-duration-format'
import VueI18n from 'vue-i18n'

Vue.use(VueI18n)
Vue.use(Vuetify)
Vue.use(helpers)
Vue.use(typy)
Vue.use(shortkey)
Vue.use(logger)
Vue.use(Vuex)
// portal-value isn't needed in test env
if (process.env.NODE_ENV !== 'test') Vue.use(PortalVue)
Vue.use(VueMoment)
momentDurationFormatSetup(Vue.moment)
