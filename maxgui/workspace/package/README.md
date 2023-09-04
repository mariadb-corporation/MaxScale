# MaxScale Workspace UI

This package only supports webpack 4 and Vue.js 2.

## Installation

```bash
npm i mxs-workspace && npm i babel-plugin-transform-class-properties --save-dev
```

```js
// babel.config.js
module.exports = {
    presets: ['@vue/cli-plugin-babel/preset'],
    plugins: ['@babel/plugin-proposal-class-properties'],
}
```

```js
// vue.config.js
module.exports = {
    transpileDependencies: ['sql-formatter'],
}
```

MaxScale Workspace peerDependencies

| Dependency                   | Version |
| ---------------------------- | :-----: |
| @mdi/font                    |  7.1.x  |
| @vuex-orm/core               | 0.36.x  |
| axios                        | 0.27.x  |
| browser-fs-access            | 0.31.x  |
| chart.js                     |  3.9.x  |
| chartjs-adapter-date-fns     |  3.0.x  |
| chartjs-plugin-trendline     |  2.0.x  |
| date-fns                     | 2.29.x  |
| deep-diff                    |  1.0.x  |
| html2canvas                  |  1.4.x  |
| dbgate-query-splitter        |  4.9.x  |
| immutability-helper          |  3.1.x  |
| localforage                  | 1.10.x  |
| lodash                       | 4.17.x  |
| monaco-editor                | 0.33.x  |
| monaco-editor-webpack-plugin |  7.1.x  |
| sql-formatter                | 13.0.x  |
| stacktrace-parser            |  0.1.x  |
| typy                         |  3.3.x  |
| uuid                         |  8.3.x  |
| vue                          |   2.x   |
| vue-chartjs                  |  4.1.x  |
| vue-i18n                     |   8.x   |
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
import VueI18n from 'vue-i18n'
import Vuex from 'vuex'
import Vuetify from 'vuetify/lib'
import { Resize, Ripple } from 'vuetify/lib/directives'
import MxsWorkspace from 'mxs-workspace'
import vuetify from '@/plugins/vuetify'
import i18n from '@/plugins/i18n'
import App from '@/App.vue'

Vue.use(shortkey)
Vue.use(typy)
Vue.use(VueI18n)
Vue.use(Vuex)
Vue.use(Vuetify, { directives: { Resize, Ripple } })
Vue.use(Vuetify)
/* store and i18n must be valid and defined.
 * e.g. Vue.use(Vuex) is called before importing store.
 */
Vue.use(MxsWorkspace, {
    store: require('./store/index').default,
    i18n: require('@/plugins/i18n').default,
    /**
     * a list of component name to be hidden, for now, only
     * `wke-nav-ctr` is a valid option.
     */
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

## Webpack config

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

## Components props, slots, events

### mxs-workspace

This component is registered globally.

#### Props

| Name                  |  Type   | Default |              Description              |
| --------------------- | :-----: | :-----: | :-----------------------------------: |
| disableRunQueries     | boolean |  false  |    Disable the "Run Queries" task     |
| disableDataMigration  | boolean |  false  |   Disable the "Data Migration" task   |
| runQueriesSubtitle    | string  |   ''    |  Subtitle of the "Run Queries" task   |
| dataMigrationSubtitle | string  |   ''    | Subtitle of the "Data Migration" task |

#### Slots

| Name                              |                   Description                   |
| --------------------------------- | :---------------------------------------------: |
| blank-worksheet-task-cards-bottom |            Slot below the task cards            |
| query-tab-nav-toolbar-right       | Slot in the right side of the query tab toolbar |

### ConfirmLeaveDlg

Built-in dialog component to prompt users when they attempt to leave a page with
open connections.

#### Props

| Name  |  Type   | Default |                         Description                         |
| ----- | :-----: | :-----: | :---------------------------------------------------------: |
| value | boolean |  false  | Controls whether the dialog is visible or hidden. (v-model) |

#### Events

| Name         | Parameter             |                      Description                       |
| ------------ | --------------------- | :----------------------------------------------------: |
| on-confirm   | confirmDelAll:boolean |  Event that fires when clicking the "Confirm" button.  |
| after-close  |                       | Event that fires after clicking the close icon button. |
| after-cancel |                       |  Event that fires after clicking the "Cancel" button.  |

### MigrCreateDlg

Built-in dialog component for creating a migration task.

#### Props

| Name       |   Type   | Required |                     Description                     |
| ---------- | :------: | :------: | :-------------------------------------------------: |
| handleSave | function |   true   | Event that fires when clicking the "Create" button. |

#### Slots

| Name         |            Description             |
| ------------ | :--------------------------------: |
| form-prepend | Slot above the built-in name input |

## Example of using the `mxs-workspace` and its sub components

The `mxs-workspace` component is registered at global scope, so it can be used
without importing.

Certain components can be manually imported and placed somewhere else outside
the `mxs-workspace` component. At the moment, the following components in the below
example are importable.

```vue
<template>
    <div class="mxs-workspace-page fill-height">
        <mxs-workspace />
        <confirm-leave-dlg
            v-model="isConfDlgOpened"
            @on-confirm="onConfirm"
            @after-close="cancelLeave"
            @after-cancel="cancelLeave"
        />
        <conn-dlg v-model="isConnDlgOpened" :handleSave="openConn" />
        <migr-create-dlg :handleSave="createEtlTask">
            <template v-slot:form-prepend>
                <!-- Add other input here. e.g. Input to determine MaxScale API. -->
            </template>
        </migr-create-dlg>
    </div>
</template>

<script>
import ConnDlg from '@components/ConnDlgCtr.vue'
import { models, ConfirmLeaveDlg, MigrCreateDlg } from 'mxs-workspace'
import { mapState, mapMutations } from 'vuex'

export default {
    name: 'maxscale-workspace',
    components: {
        ConnDlg,
        ConfirmLeaveDlg,
        MigrCreateDlg,
    },
    data() {
        return {
            isConfDlgOpened: false,
            to: '',
        }
    },
    computed: {
        ...mapState({
            conn_dlg: state => state.mxsWorkspace.conn_dlg,
        }),
        allConns() {
            return models.QueryConn.all()
        },
        isConnDlgOpened: {
            get() {
                return this.conn_dlg.is_opened
            },
            set(v) {
                this.SET_CONN_DLG({ ...this.conn_dlg, is_opened: v })
            },
        },
    },
    beforeRouteLeave(to, from, next) {
        if (this.to) {
            next()
        } else {
            this.to = to
            if (this.allConns.length === 0) this.leavePage()
            else this.isConfDlgOpened = true
        }
    },
    async beforeCreate() {
        await this.$store.dispatch('mxsWorkspace/initWorkspace')
    },
    async created() {
        this.initApis()
        await models.QueryConn.dispatch('validateConns')
    },
    methods: {
        ...mapMutations({
            SET_CONN_DLG: 'mxsWorkspace/SET_CONN_DLG',
        }),
        /**
         * When the workspace UI is used with multiple MaxScales, the base URL is dynamic,
         * therefore, to make it work properly, use the `WorksheetTmp` model for
         * setting axios request config. The `WorksheetTmp` model is stored in memory
         * so your auth token can be secured.
         */
        initApis() {
            models.Worksheet.all().forEach(worksheet => {
                /**
                 * If a worksheet is not yet connected to any MaxScales, you must skip the below method
                 * otherwise it'll use the root url as the baseURL for validating alive connections.
                 */
                setRequestConfig(worksheet.id)
            })
        },
        async onConfirm(shouldDelAll) {
            // Connection cleans up
            if (shouldDelAll) await models.QueryConn.dispatch('disconnectAll')
            this.leavePage()
        },
        leavePage() {
            this.$router.push(this.to)
        },
        cancelLeave() {
            this.to = null
        },
        setRequestConfig(worksheetId) {
            models.WorksheetTmp.update({
                where: worksheetId,
                data: {
                    // [request-config](https://github.com/axios/axios#request-config)
                    request_config: {
                        withCredentials: true,
                        baseURL: 'MAXSCALE API URL',
                        headers: {
                            Authorization: 'Bearer TOKEN',
                        },
                    },
                },
            })
        },
        /**
         * https://github.com/mariadb-corporation/MaxScale/blob/22.08/Documentation/REST-API/Resources-SQL.md#open-sql-connection-to-server
         * @param {Object} params.body
         * @param {Object} params.meta - extra info about the connection.
         * @param {String} params.meta.name - connection name.
         */
        async openConn(params) {
            this.setRequestConfig(models.Worksheet.getters('activeId'))
            await models.QueryConn.dispatch('handleOpenConn', params)
        },
        createEtlTask(name) {
            this.setRequestConfig(models.Worksheet.getters('activeId'))
            models.EtlTask.dispatch('createEtlTask', name)
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

## Reserved keywords

Certain keywords are reserved in order to make the workspace work properly.

| Keywords           |                         For                         |
| ------------------ | :-------------------------------------------------: |
| mxs                |                  i18n json locale                   |
| mxs_icon_name      | The workspace icons, they have `mxs_`as the prefix. |
| mxs-component-name |    Global Vue components with`mxs-`as the prefix    |
| mxs-workspace      |                Global Vue component                 |
| editorsMem         |                     Vuex module                     |
| fileSysAccess      |                     Vuex module                     |
| mxsApp             |                     Vuex module                     |
| queryConnsMem      |                     Vuex module                     |
| prefAndStorage     |                     Vuex module                     |
| mxsWorkspace       |                     Vuex module                     |
| queryResultsMem    |                     Vuex module                     |
| ORM                |                     Vuex module                     |
| store.vue          |      Vue.prototype is registered in Vuex store      |
| \$mxs_t            |                Vue instance property                |
| \$mxs_tc           |                Vue instance property                |
| \$mxs_te           |                Vue instance property                |
| \$mxs_d            |                Vue instance property                |
| \$mxs_n            |                Vue instance property                |
| \$queryHttp        |                Vue instance property                |
| \$helpers          |                Vue instance property                |
| \$logger           |                Vue instance property                |

## How to publish the package

There is an issue with the `rollup-plugin-monaco-editor` package regarding
their [peer dependencies resolution](https://github.com/chengcyber/rollup-plugin-monaco-editor/issues/11). The workaround is to use `npm ci --force`.

At the root directory that is `/maxgui`

```
npm ci && cd workspace/package && npm ci --force && npm publish
```
