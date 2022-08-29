# MaxScale Query Editor UI

## Installation

//TODO: Add this when it's published in npm

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
import MaxScaleQueryEditor from 'maxscale-query-editor'
import 'maxscale-query-editor/dist/maxscale-query-editor.esm.css'
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
Vue.use(MaxScaleQueryEditor, {
    store: require('./store/index').default,
    i18n: require('@/plugins/i18n').default,
    // a list of component name to be hidden
    hidden_comp: ['wke-nav-ctr'],
})

new Vue({
    vuetify,
    i18n,
    render: (h) => h(App),
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
    locales.keys().forEach((key) => {
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
        require.context('maxscale-query-editor/dist/locales', true, /[A-Za-z0-9-_,\s]+\.json$/i)
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
import queryEditorIcons from 'maxscale-query-editor/dist/icons'
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
import queryEditorPersistPlugin from 'maxscale-query-editor/dist/persistPlugin'

export default new Vuex.Store({
    namespaced: true,
    plugins: [
        (store) => {
            store.vue = Vue.prototype
        },
        queryEditorPersistPlugin,
    ],
})
```

Use `maxscale-query-editor` component

`maxscale-query-editor` is registered at global scope.

```vue
<!-- SkyQuery.vue -->
<template>
    <maxscale-query-editor
        ref="queryEditor"
        class="query-editor-page"
        @leave-page="$router.push($event)"
    />
</template>

<script>
export default {
    name: 'query-page',
    // router hook to show confirm leaving page dialog
    async beforeRouteLeave(to, from, next) {
        this.$refs.queryEditor.$refs.queryEditor.beforeRouteLeaveHandler(to, from, next)
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

## Use `maxscale-query-editor` sub components

If the `hidden_comp` option is provided when registering the plugin,
the components in the list won't be rendered. e.g. `hidden_comp: ['wke-nav-ctr']`.
Certain components can be manually imported and placed somewhere else outside
the `maxscale-query-editor` component. At the moment, only `query-cnf-gear-btn`
and `min-max-btn` are importable.

```vue
<!-- SkyQuery.vue -->
<template>
    <maxscale-query-editor
        ref="queryEditor"
        class="query-editor-page"
        @leave-page="$router.push($event)"
    >
        <template v-slot:query-editor-top>
            <div class="d-flex flex-wrap">
                <v-spacer />
                <query-cnf-gear-btn />
                <min-max-btn />
            </div>
        </template>
    </maxscale-query-editor>
</template>

<script>
import { MinMaxBtn, QueryCnfGearBtn } from 'maxscale-query-editor'
export default {
    name: 'query-page',
    components: {
        QueryCnfGearBtn,
        MinMaxBtn,
    },
    async beforeRouteLeave(to, from, next) {
        this.$refs.queryEditor.$refs.queryEditor.beforeRouteLeaveHandler(to, from, next)
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

## Reserved keywords

Certain keywords are reserved in order to make the query editor work properly.

| Keywords              |                        For                         |
| --------------------- | :------------------------------------------------: |
| queryEditor           |                  i18n json locale                  |
| mxs_icon_name         | Query editor icons, they have `mxs_`as the prefix. |
| mxs-component-name    |   Global Vue components with`mxs-`as the prefix    |
| maxscale-query-editor |                Global Vue component                |
| appNotifier           |                    Vuex module                     |
| wke                   |                    Vuex module                     |
| queryConn             |                    Vuex module                     |
| editor                |                    Vuex module                     |
| schemaSidebar         |                    Vuex module                     |
| queryResult           |                    Vuex module                     |
| querySession          |                    Vuex module                     |
| queryPersisted        |                    Vuex module                     |
| queryEditorConfig     |                    Vuex module                     |
| \$mxs_t               |               Vue instance property                |
| \$mxs_tc              |               Vue instance property                |
| \$mxs_te              |               Vue instance property                |
| \$mxs_d               |               Vue instance property                |
| \$mxs_n               |               Vue instance property                |
| \$queryHttp           |               Vue instance property                |
| \$helpers             |               Vue instance property                |
| \$logger              |               Vue instance property                |

## How to pack the package

At the root directory that is `/maxgui`

```
npm ci && cd queryEditor/package && npm ci --force && npm run pack
```
