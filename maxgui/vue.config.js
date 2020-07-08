/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

const path = require('path')
const fs = require('fs')
const { gitDescribeSync } = require('git-describe')

process.env.VUE_APP_VERSION = require('./package.json').version
try {
    process.env.VUE_APP_GIT_COMMIT = gitDescribeSync().hash
} catch (e) {
    process.env.VUE_APP_GIT_COMMIT = 'UNKNOWN'
}
module.exports = {
    chainWebpack: config => {
        const types = ['vue-modules', 'vue', 'normal-modules', 'normal']
        types.forEach(type => addStyleResource(config.module.rule('scss').oneOf(type)))
        config.module.rule('js').exclude.add(/\.worker\.js$/)
        config.resolve.alias.set('@tests', path.resolve(__dirname, 'tests'))
        config.when(process.env.NODE_ENV === 'development', config => {
            // devtool
            config.merge({
                devtool: 'source-map',
                devServer: {
                    https: {
                        key: fs.readFileSync('./.certs/localhost+1-key.pem'),
                        cert: fs.readFileSync('./.certs/localhost+1.pem'),
                    },
                    progress: false,
                    port: 8000,
                    headers: {
                        'Access-Control-Allow-Origin': '*',
                    },
                    public: 'https://localhost:8000/',
                    proxy: {
                        '^/': {
                            changeOrigin: true,
                            target: process.env.VUE_APP_API,
                        },
                    },
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

        config.resolve.modules.add(path.resolve('./src'), path.resolve('./node_modules'))

        /*  
            To strip all locales except “en”, and ...
            (“en” is built into Moment and can’t be removed) 
        */
        config.plugin('MomentLocalesPlugin').use(require('moment-locales-webpack-plugin'), [
            {
                localesToKeep: [], //e.g. 'ru', 'vi'
            },
        ])
    },

    transpileDependencies: ['vuetify'],

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
}
function addStyleResource(rule) {
    rule.use('style-resource')
        .loader('style-resources-loader')
        .options({
            patterns: [path.resolve(__dirname, './src/styles/constants.scss')],
        })
}
