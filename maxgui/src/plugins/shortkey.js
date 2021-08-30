import Vue from 'vue'
import VueShortKey from 'vue-shortkey'

Vue.use(VueShortKey, { prevent: ['input', 'textarea'] })
