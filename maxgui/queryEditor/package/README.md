# MaxScale Query Editor UI

This package only supports webpack 4 and Vue.js 2.

## Installation

```bash
npm i mxs-query-editor
```

Query editor peerDependencies

| Dependency                   | Version |
| ---------------------------- | :-----: |
| @mdi/font                    |  7.0.x  |
| axios                        | 0.27.x  |
| browser-fs-access            | 0.30.x  |
| chart.js                     |  2.9.x  |
| chartjs-plugin-trendline     |  0.2.x  |
| deep-diff                    |  1.0.x  |
| dbgate-query-splitter        |  4.9.x  |
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

Registering MaxScale Query Editor plugin

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
import MxsQueryEditor from 'mxs-query-editor'
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
Vue.use(MxsQueryEditor, {
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

Merging locale messages. Query editor scopes its messages in the `queryEditor`
key to prevent existing keys from being overwritten when merging locales.

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
    // Get query editor messages.
    const queryEditorMsgs = getMsgs(
        require.context('mxs-query-editor/dist/locales', true, /[A-Za-z0-9-_,\s]+\.json$/i)
    )
    const messages = getMsgs(require.context('@/locales', true, /[A-Za-z0-9-_,\s]+\.json$/i))
    return merge(messages, queryEditorMsgs)
}

export default new VueI18n({
    locale: process.env.VUE_APP_I18N_LOCALE || 'en',
    fallbackLocale: process.env.VUE_APP_I18N_FALLBACK_LOCALE || 'en',
    messages: loadLocaleMessages(),
})
```

Merging query editor custom icons. Query editor registers its custom icons via
Vuetify. These icons have an `mxs_` prefix to prevent existing icons from being
overwritten. e.g. `$vuetify.icons.mxs_edit`

```js
// plugins/vuetify.js
import Vuetify from 'vuetify/lib'
import icons from 'icons'
import queryEditorIcons from 'mxs-query-editor/dist/icons'
import i18n from './i18n'
import vuetifyTheme from './vuetifyTheme'
import '@mdi/font/css/materialdesignicons.css'

export default new Vuetify({
    icons: {
        iconfont: 'mdi',
        values: { ...icons, ...queryEditorIcons },
    },
    theme: {
        themes: {
            light: {
                primary: '#0e9bc0',
                secondary: '#E6EEF1',
                accent: '#2f99a3',
                ['accent-dark']: '#0b718c',
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

Register query editor vuex plugins. Query editor internally uses `vuex-persist`
and `localforage` packages to persist its states. So this needs to be
registered manually.

```js
// store/index.js
import Vue from 'vue'
import Vuex from 'vuex'
import { queryEditorStorePlugins } from 'mxs-query-editor'

export default new Vuex.Store({
    namespaced: true,
    plugins: queryEditorStorePlugins,
})
```

## Use `mxs-query-editor` and its sub components

The `mxs-query-editor` component is registered at global scope, so it can be used
without importing.

If the `hidden_comp` option is provided when registering the plugin,
the components in the list won't be rendered. e.g. `hidden_comp: ['wke-nav-ctr']`.
For now, only `wke-nav-ctr` will be applied.

Certain components can be manually imported and placed somewhere else outside
the `mxs-query-editor` component. At the moment, the following components in the below
example are importable.

```vue
<!-- SkyQuery.vue -->
<template>
    <div class="query-editor-page">
        <ConfirmLeaveDlg
            v-model="isConfDlgOpened"
            :onSave="onLeave"
            :shouldDelAll.sync="shouldDelAll"
            @on-close="cancelLeave"
            @on-cancel="cancelLeave"
        />
        <ReconnDlgCtr :onReconnectCb="onReconnectCb" />
        <mxs-query-editor>
            <!-- Slot for placing content at the top of the query editor that is above the worksheet
                 navigation tabs.
            -->
            <template v-slot:query-editor-top>
                <div class="d-flex flex-wrap">
                    <v-spacer />
                    <QueryCnfGearBtn />
                    <MinMaxBtnCtr />
                </div>
            </template>
            <!-- txt-editor-toolbar-right-slot`: Slot for placing content on the right side of
                 the toolbar below the query navigation tabs in TXT_EDITOR and DDL_EDITOR modes
            -->
            <template v-slot:txt-editor-toolbar-right-slot>
                <div class="d-flex flex-wrap">
                    <v-spacer />
                    <QueryCnfGearBtn />
                    <MinMaxBtnCtr />
                </div>
            </template>
            <template v-slot:ddl-editor-toolbar-right-slot>
                <div class="d-flex flex-wrap">
                    <v-spacer />
                    <QueryCnfGearBtn />
                    <MinMaxBtnCtr />
                </div>
            </template>
        </mxs-query-editor>
    </div>
</template>

<script>
import { mapActions, mapState } from 'vuex'
import { MinMaxBtnCtr, QueryCnfGearBtn, ConfirmLeaveDlg, ReconnDlgCtr } from 'mxs-query-editor'
export default {
    name: 'query-page',
    components: {
        QueryCnfGearBtn, // Query configuration button
        MinMaxBtnCtr, // Toggle full-screen mode button
        ConfirmLeaveDlg, // confirmation on leave dialog
        ReconnDlgCtr, // reconnect dialog
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
            sql_conns: state => state.queryConn.sql_conns,
        }),
    },
    beforeRouteLeave(to, from, next) {
        if (this.to) {
            next()
        } else {
            this.to = to
            if (Object.keys(this.sql_conns).length === 0) this.leavePage()
            else {
                this.shouldDelAll = true
                this.isConfDlgOpened = true
            }
        }
    },
    async created() {
        await this.validateConns({ sqlConns: this.sql_conns })
    },
    methods: {
        ...mapActions({
            disconnectAll: 'queryConn/disconnectAll',
            validateConns: 'queryConn/validateConns',
        }),
        async onLeave() {
            if (this.shouldDelAll) await this.disconnectAll()
            this.leavePage()
        },
        leavePage() {
            this.$router.push(this.to)
        },
        cancelLeave() {
            this.to = null
        },
        async onReconnectCb() {
            await this.validateConns({ sqlConns: this.sql_conns, silentValidation: true })
        },
    },
}
</script>
<style lang="scss" scoped>
.query-editor-page {
    width: 100%;
    height: 100%;
}
</style>
```

## Connection cleans up

This package does not automatically clean up the opened connection when the
`mxs-query-editor` component is destroyed. e.g. When leaving the page, the opened
connections are kept.

If all connections belong to one MaxScale, use the below vuex action to clear
all connections:

```js
store.dispatch('queryConn/disconnectAll', { root: true })
```

If connections belongs to multiple MaxScale, use the below example to clear
all connections of a MaxScale:

```js
for (const { id = '' } of connections_of_a_maxscale) {
    commit(
        'queryEditorConfig/SET_AXIOS_OPTS',
        {
            baseURL: maxscale_API_URL,
        },
        { root: true }
    )
    await dispatch('disconnect', { showSnackbar: false, id })
}
```

## Build the editor

The query editor internally uses [monaco-editor](https://github.com/microsoft/monaco-editor)
and it requires configuration steps from webpack.

```js
//vue.config.js
const monacoConfig = require('mxs-query-editor/dist/buildConfig/monaco.js')
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
store.commit('queryEditorConfig/SET_AXIOS_OPTS', options)
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

## Change authentication cookies expired time

The default expired time for the authentication cookies of SQL connections is
24 hours (86400). To change it, committing the following mutation:

`store.commit('queryEditorConfig/SET_AUTH_COOKIES_MAX_AGE', 86400)`

## Reserved keywords

Certain keywords are reserved in order to make the query editor work properly.

| Keywords           |                        For                         |
| ------------------ | :------------------------------------------------: |
| queryEditor        |                  i18n json locale                  |
| mxs_icon_name      | Query editor icons, they have `mxs_`as the prefix. |
| mxs-component-name |   Global Vue components with`mxs-`as the prefix    |
| mxs-query-editor   |                Global Vue component                |
| mxsApp             |                    Vuex module                     |
| wke                |                    Vuex module                     |
| queryConn          |                    Vuex module                     |
| editor             |                    Vuex module                     |
| schemaSidebar      |                    Vuex module                     |
| queryResult        |                    Vuex module                     |
| querySession       |                    Vuex module                     |
| queryPersisted     |                    Vuex module                     |
| queryEditorConfig  |                    Vuex module                     |
| store.vue          |                     Vuex store                     |
| \$mxs_t            |               Vue instance property                |
| \$mxs_tc           |               Vue instance property                |
| \$mxs_te           |               Vue instance property                |
| \$mxs_d            |               Vue instance property                |
| \$mxs_n            |               Vue instance property                |
| \$queryHttp        |               Vue instance property                |
| \$helpers          |               Vue instance property                |
| \$logger           |               Vue instance property                |

## How to publish the package

At the root directory that is `/maxgui`

```
npm ci && cd queryEditor/package && npm ci --force && npm publish
```
