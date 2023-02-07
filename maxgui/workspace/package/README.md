# MaxScale Workspace UI

This package only supports webpack 4 and Vue.js 2.

## Installation

```bash
npm i mxs-workspace
```

MaxScale Workspace peerDependencies

| Dependency                   | Version |
| ---------------------------- | :-----: |
| @mdi/font                    |  7.0.x  |
| @vuex-orm/core               | 0.36.x  |
| axios                        | 0.27.x  |
| browser-fs-access            | 0.31.x  |
| chart.js                     |  2.9.x  |
| chartjs-plugin-trendline     |  0.2.x  |
| deep-diff                    |  1.0.x  |
| immutability-helper          |  3.1.x  |
| localforage                  | 1.10.x  |
| lodash                       | 4.17.x  |
| monaco-editor                | 0.33.x  |
| monaco-editor-webpack-plugin |  7.0.x  |
| sql-formatter                |  4.0.x  |
| stacktrace-parser            |  0.1.x  |
| typy                         |  3.3.x  |
| uuid                         |  8.3.x  |
| vue                          |   2.x   |
| vue-chartjs                  |  3.5.x  |
| vue-i18n                     |   8.x   |
| vue-moment                   |   4.x   |
| vue-shortkey                 |   3.x   |
| vue-template-compiler        |   2.x   |
| vuetify                      |  2.6.x  |
| vuex                         |  3.6.x  |
| vuex-persist                 |  3.1.x  |

## Example of registering the plugin

Registering MaxScale Workspace plugin

```js
// main.js
import Vue from 'vue'
import shortkey from '@/plugins/shortkey'
import typy from '@/plugins/typy'
import VueMoment from 'vue-moment'
import VueI18n from 'vue-i18n'
import Vuex from 'vuex'
import Vuetify from 'vuetify/lib'
import { Resize } from 'vuetify/lib/directives'
import MxsWorkspace from 'mxs-workspace'
import vuetify from '@/plugins/vuetify'
import i18n from '@/plugins/i18n'
import App from '@/App.vue'

Vue.use(shortkey)
Vue.use(typy)
Vue.use(VueMoment)
Vue.use(VueI18n)
Vue.use(Vuex)
Vue.use(Vuetify, { directives: { Resize } })
Vue.use(Vuetify)
/* store and i18n must be valid and defined.
 * e.g. Vue.use(Vuex) is called before importing store.
 */
Vue.use(MxsWorkspace, {
    store: require('./store/index').default,
    i18n: require('@/plugins/i18n').default,
    // a list of component name to be hidden
    hidden_comp: ['wke-nav-ctr'],
})

new Vue({
    vuetify,
    i18n,
    render: h => h(App),
}).$mount('#app')
```

When merging locales, the workspace scopes its messages in the `mxs` key to avoid
existing keys from being overwritten. Therefore, make sure your local json string
does not have this key.

```js
// plugins/i18n.js
import VueI18n from 'vue-i18n'
const merge = require('lodash/merge')
function getMsgs(locales) {
    const messages = {}
    locales.keys().forEach(key => {
        const matched = key.match(/([A-Za-z0-9-_]+)\./i)
        if (matched && matched.length > 1) {
            const locale = matched[1]
            messages[locale] = locales(key)
        }
    })
    return messages
}
function loadLocaleMessages() {
    // Get workspace messages.
    const workspaceMsgs = getMsgs(
        require.context('mxs-workspace/dist/locales', true, /[A-Za-z0-9-_,\s]+\.json$/i)
    )
    const messages = getMsgs(require.context('@/locales', true, /[A-Za-z0-9-_,\s]+\.json$/i))
    return merge(messages, workspaceMsgs)
}

export default new VueI18n({
    locale: process.env.VUE_APP_I18N_LOCALE || 'en',
    fallbackLocale: process.env.VUE_APP_I18N_FALLBACK_LOCALE || 'en',
    messages: loadLocaleMessages(),
})
```

Merging workspace custom icons. The workspace registers its custom icons via
Vuetify. These icons have an `mxs_` prefix to prevent existing icons from being
overwritten. e.g. `$vuetify.icons.mxs_edit`

```js
// plugins/vuetify.js
import Vuetify from 'vuetify/lib'
import icons from 'icons'
import workspaceIcons from 'mxs-workspace/dist/icons'
import i18n from './i18n'
import vuetifyTheme from './vuetifyTheme'
import '@mdi/font/css/materialdesignicons.css'

export default new Vuetify({
    icons: {
        iconfont: 'mdi',
        values: { ...icons, ...workspaceIcons },
    },
    theme: {
        themes: {
            light: {
                primary: '#0e9bc0',
                secondary: '#E6EEF1',
                accent: '#2f99a3',
                success: '#7dd012',
                error: '#eb5757',
                warning: '#f59d34',
                info: '#2d9cdb',
                anchor: '#2d9cdb',
                navigation: '#424F62',
                ['deep-ocean']: '#003545',
                ['blue-azure']: '#0e6488',
            },
        },
    },
    lang: {
        t: (key, ...params) => i18n.t(key, params),
    },
})
```

Register workspace vuex plugins. The workspace internally uses `vuex-orm, vuex-persist`
and `localforage` packages to persist its states. So this needs to be
registered manually.

```js
// store/index.js
import Vue from 'vue'
import Vuex from 'vuex'
import { workspaceStorePlugins } from 'mxs-workspace'

export default new Vuex.Store({
    namespaced: true,
    plugins: workspaceStorePlugins,
})
```

## Use `mxs-workspace` and its sub components

The `mxs-workspace` component is registered at global scope, so it can be used
without importing.

If the `hidden_comp` option is provided when registering the plugin,
the components in the list won't be rendered. e.g. `hidden_comp: ['wke-nav-ctr']`.
For now, only `wke-nav-ctr` will be applied.

Certain components can be manually imported and placed somewhere else outside
the `mxs-workspace` component. At the moment, the following components in the below
example are importable.

```vue
<template>
    <div class="mxs-workspace-page fill-height">
        <mxs-workspace />
        <confirm-leave-dlg
            v-model="isConfDlgOpened"
            :onSave="onLeave"
            :shouldDelAll.sync="shouldDelAll"
            @on-close="cancelLeave"
            @on-cancel="cancelLeave"
        />
        <conn-dlg v-model="isConnDlgOpened" :handleSave="handleOpenConn" />
    </div>
</template>

<script>
import ConnDlg from '@components/ConnDlgCtr.vue'
import { models, ConfirmLeaveDlg } from 'mxs-workspace'
import { mapState, mapMutations } from 'vuex'

export default {
    name: 'maxscale-workspace',
    components: {
        ConfirmLeaveDlg, // built-in confirmation on leave dialog
        ConnDlg,
    },
    data() {
        return {
            isConfDlgOpened: false,
            shouldDelAll: true,
            to: '',
        }
    },
    computed: {
        ...mapState({
            is_conn_dlg_opened: state => state.mxsWorkspace.is_conn_dlg_opened,
        }),
        allConns() {
            return models.QueryConn.all()
        },
        isConnDlgOpened: {
            get() {
                return this.is_conn_dlg_opened
            },
            set(v) {
                this.SET_IS_CONN_DLG_OPENED(v)
            },
        },
    },
    beforeRouteLeave(to, from, next) {
        if (this.to) {
            next()
        } else {
            this.to = to
            if (this.allConns.length === 0) this.leavePage()
            else {
                this.shouldDelAll = true
                this.isConfDlgOpened = true
            }
        }
    },
    async beforeCreate() {
        await this.$store.dispatch('mxsWorkspace/initWorkspace')
    },
    async created() {
        this.SET_CONNS_TO_BE_VALIDATED(this.allConns)
        await models.QueryConn.dispatch('validateConns')
    },
    methods: {
        ...mapMutations({
            SET_IS_CONN_DLG_OPENED: 'mxsWorkspace/SET_IS_CONN_DLG_OPENED',
            SET_CONNS_TO_BE_VALIDATED: 'mxsWorkspace/SET_CONNS_TO_BE_VALIDATED',
        }),
        async onLeave() {
            if (this.shouldDelAll) await models.QueryConn.dispatch('disconnectAll')
            this.leavePage()
        },
        leavePage() {
            this.$router.push(this.to)
        },
        cancelLeave() {
            this.to = null
        },
        /**
         * @param {Object} params.body - https://github.com/mariadb-corporation/MaxScale/blob/22.08/Documentation/REST-API/Resources-SQL.md#open-sql-connection-to-server
         * @param {Object} params.meta - extra info about the connection.
         * @param {String} params.meta.name - connection name.
         */
        async handleOpenConn(params) {
            await models.QueryConn.dispatch('openQueryEditorConn', params)
        },
    },
}
</script>
<style lang="scss" scoped>
.mxs-workspace-page {
    width: 100%;
}
</style>
```

## Connection cleans up

This package does not automatically clean up the opened connection when the
`mxs-workspace` component is destroyed. e.g. When leaving the page, the opened
connections are kept.

If all connections belong to one MaxScale, use the below vuex action to clear
all connections:

```js
import { models } from 'mxs-workspace'

await models.QueryConn.dispatch('disconnectAll')
```

If connections belongs to multiple MaxScale, use the below example to clear
all connections of a MaxScale:

```js
import { models } from 'mxs-workspace'
/**
 * When creating a connection via `models.QueryConn.dispatch('openQueryEditorConn', { body, meta })`,
 * The meta field can store information needed to identify which connections belong to which maxscale.
 * To delete all open connections from the workspace, group all connections belong to one maxscale and
 * delete one by one.
 */
for (const { id = '' } of connections_of_a_maxscale) {
    commit(
        'mxsWorkspace/SET_AXIOS_OPTS',
        {
            baseURL: maxscale_API_URL,
        },
        { root: true }
    )
    await models.QueryConn.dispatch('cascadeDisconnectWkeConn', { showSnackbar: false, id })
}
```

## Build the editor

The workspace internally uses [monaco-editor](https://github.com/microsoft/monaco-editor)
and it requires configuration steps from webpack.

```js
//vue.config.js
const monacoConfig = require('mxs-workspace/dist/buildConfig/monaco.js')
module.exports = {
    chainWebpack: config => {
        config
            .plugin('MonacoWebpackPlugin')
            .use(require('monaco-editor-webpack-plugin'), [monacoConfig])
    },
}
```

## Change axios request config options

Axios request config options can be changed at run time by committing the following
mutation:

```js
store.commit('mxsWorkspace/SET_AXIOS_OPTS', options)
```

The provided config options will be merged with the current one which has the following default options:

```js
{
    baseURL: '/',
    headers: {
        'X-Requested-With': 'XMLHttpRequest',
        'Content-Type': 'application/json',
        'Cache-Control': 'no-cache',
   },
}
```

Check [axios documentation](https://github.com/axios/axios#request-config) for available options

## Reserved keywords

Certain keywords are reserved in order to make the workspace work properly.

| Keywords           |                         For                         |
| ------------------ | :-------------------------------------------------: |
| workspace          |                  i18n json locale                   |
| mxs_icon_name      | The workspace icons, they have `mxs_`as the prefix. |
| mxs-component-name |    Global Vue components with`mxs-`as the prefix    |
| mxs-workspace      |                Global Vue component                 |
| editorsMem         |                     Vuex module                     |
| fileSysAccess      |                     Vuex module                     |
| mxsApp             |                     Vuex module                     |
| queryConnsMem      |                     Vuex module                     |
| mxsWorkspace       |                     Vuex module                     |
| prefAndStorage     |                     Vuex module                     |
| ORM                |                     Vuex module                     |
| store.vue          |                     Vuex store                      |
| \$mxs_t            |                Vue instance property                |
| \$mxs_tc           |                Vue instance property                |
| \$mxs_te           |                Vue instance property                |
| \$mxs_d            |                Vue instance property                |
| \$mxs_n            |                Vue instance property                |
| \$queryHttp        |                Vue instance property                |
| \$helpers          |                Vue instance property                |
| \$logger           |                Vue instance property                |

## How to publish the package

At the root directory that is `/maxgui`

```
npm ci && cd workspace/package && npm ci --force && npm publish
```
