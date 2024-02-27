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
const monacoConfig = require('./share/buildConfig/monaco')
const path = require('path')
const { gitDescribeSync } = require('git-describe')
const fs = require('fs')

process.env.VUE_APP_VERSION = require('./package.json').version
try {
    process.env.VUE_APP_GIT_COMMIT = gitDescribeSync().hash
} catch (e) {
    if (!process.env.VUE_APP_GIT_COMMIT) {
        process.env.VUE_APP_GIT_COMMIT = 'UNKNOWN'
    }
}
module.exports = {
    chainWebpack: config => {
        const types = ['vue-modules', 'vue', 'normal-modules', 'normal']
        types.forEach(type => addStyleResource(config.module.rule('scss').oneOf(type)))
        config.module.rule('js').exclude.add(/\.worker\.js$/)

        config.resolve.modules
            .add(path.resolve(__dirname, 'src'), 'node_modules')
            .add(path.resolve(__dirname, 'workspace'), 'node_modules')

        config.resolve.alias
            .set('@src', path.resolve(__dirname, 'src'))
            .set('@tests', path.resolve(__dirname, 'tests'))
            .set('@workspace', path.resolve(__dirname, 'workspace'))
            .set('@wsSrc', path.resolve(__dirname, 'workspace/src'))
            .set('@wsComps', path.resolve(__dirname, 'workspace/src/components'))
            .set('@wkeComps', path.resolve(__dirname, 'workspace/src/components/worksheets/'))
            .set('@wsModels', path.resolve(__dirname, 'workspace/src/store/orm/models'))
            .set('@share', path.resolve(__dirname, 'share'))

        const key = process.env.httpsKey
        const cert = process.env.httpsCert
        const isHttps = Boolean(key && cert)
        const https = isHttps
            ? { https: { key: fs.readFileSync(key), cert: fs.readFileSync(cert) } }
            : {}
        config.when(process.env.NODE_ENV === 'development', config => {
            // devtool
            config.merge({
                devtool: 'source-map',
                devServer: {
                    progress: false,
                    port: 8000,
                    headers: {
                        'Access-Control-Allow-Origin': '*',
                    },
                    proxy: {
                        '^/': {
                            changeOrigin: true,
                            target: process.env.VUE_APP_API,
                        },
                    },
                    ...https,
                },
            })
        })

        config.when(process.env.NODE_ENV === 'production', config => {
            /*
            optimization in production mode only as unit testing is broken when
            splitChunks is configured
            */
            config.optimization.splitChunks({
                chunks: 'all',
                maxSize: 250000,
                cacheGroups: {
                    vendor: {
                        test: /[\\/]node_modules[\\/]/,
                        name(module) {
                            const packageName = module.context.match(
                                /[\\/]node_modules[\\/](.*?)([\\/]|$)/
                            )[1]
                            return `npm.${packageName.replace('@', '')}`
                        },
                    },
                },
            })
            config.performance.hints(false)
        })

        config.module
            .rule('worker-loader')
            .test(/\.worker\.js$/)
            .use('worker-loader')
            .loader('worker-loader')
            .options({
                inline: true,
                fallback: false,
                name: '[name]:[hash:8].js',
            })
            .end()

        config
            .plugin('MonacoWebpackPlugin')
            .use(require('monaco-editor-webpack-plugin'), [monacoConfig])
    },

    transpileDependencies: ['vuetify', 'vuex-persist', 'sql-formatter'],

    outputDir: `${process.env.buildPath}/gui`,

    pluginOptions: {
        i18n: {
            locale: 'en',
            fallbackLocale: 'en',
            localeDir: 'locales',
            enableInSFC: true,
        },
    },

    productionSourceMap: false,
    ...(process.env.NODE_ENV === 'production' ? { css: { extract: { ignoreOrder: true } } } : {}),
}
function addStyleResource(rule) {
    rule.use('style-resource')
        .loader('style-resources-loader')
        .options({
            patterns: [path.resolve(__dirname, 'share/styles/colors.scss')],
        })
}
