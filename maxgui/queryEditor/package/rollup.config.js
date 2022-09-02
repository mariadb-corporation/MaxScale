import fs from 'fs'
import path from 'path'
import vuePlugin from 'rollup-plugin-vue'
import alias from '@rollup/plugin-alias'
import commonjs from '@rollup/plugin-commonjs'
import resolve from '@rollup/plugin-node-resolve'
import replace from '@rollup/plugin-replace'
import scss from 'rollup-plugin-scss'
import babel from '@rollup/plugin-babel'
import vuetify from 'rollup-plugin-vuetify'
import monaco from 'rollup-plugin-monaco-editor'
import copy from 'rollup-plugin-copy'
import dotenv from 'rollup-plugin-dotenv'
import { terser } from 'rollup-plugin-terser'

const projectRoot = path.resolve(__dirname, '../../')
const rootSrcPath = path.resolve(projectRoot, 'src')
const queryEditorPath = path.resolve(__dirname, '../')
const queryEditorSrcPath = `${queryEditorPath}/src`
const sharePath = path.resolve(projectRoot, 'share')

const monacoConfig = require(`${sharePath}/buildConfig/monaco`)

// Get browserslist config
const esbrowserslist = fs
    .readFileSync(`${projectRoot}/.browserslistrc`)
    .toString()
    .split('\n')

const resolver = resolve({ extensions: ['.js', '.vue', '.json', '.scss'] })

const scssVariables = `@import "${sharePath}/styles/colors.scss";
                       @import "${queryEditorSrcPath}/styles/variables.scss";`

// Refer to https://rollupjs.org/guide/en/#warning-treating-module-as-external-dependency
const external = [
    // list external dependencies, exactly the way it is written in the import statement.
    'axios',
    'browser-fs-access',
    'chart.js',
    'chartjs-plugin-trendline',
    'localforage',
    'lodash',
    'moment',
    'sql-formatter',
    'sql-formatter/lib/core/Formatter',
    'sql-formatter/lib/core/Tokenizer',
    'uuid',
    'vue',
    'vue-chartjs',
    'vue-moment',
    'vuetify',
    'vuetify/lib',
    'vuex',
    'vuex-persist',
]
const copyright = fs.readFileSync(path.resolve(projectRoot, '../COPYRIGHT')).toString()
function copyrightBanner() {
    return {
        name: 'copyright-banner',
        renderChunk(code) {
            return copyright + code
        },
    }
}
// Export config
export default [
    {
        input: `${queryEditorSrcPath}/mxs-query-editor.js`,
        output: [
            { file: 'dist/mxs-query-editor.esm.js', format: 'esm', exports: 'named' },
            { file: 'dist/mxs-query-editor.js', format: 'cjs', exports: 'named' },
        ],
        treeshake: true,
        external,
        plugins: [
            dotenv({ cwd: projectRoot }),
            replace({
                preventAssignment: true,
                'process.env.NODE_ENV': JSON.stringify('production'),
            }),
            resolver,
            alias({
                entries: [
                    { find: '@rootSrc', replacement: rootSrcPath },
                    { find: '@queryEditor', replacement: queryEditorPath },
                    { find: '@queryEditorSrc', replacement: queryEditorSrcPath },
                    { find: '@share', replacement: sharePath },
                ],
                resolver,
            }),
            vuePlugin({
                template: { isProduction: true },
                data: {
                    // This helps to inject variables in each <style> tag of every Vue SFC
                    scss: () => scssVariables,
                    vue: () => scssVariables,
                },
            }),
            scss({
                prefix: scssVariables,
                output: './dist/mxs-query-editor.css',
                outputStyle: 'compressed',
                insert: true,
            }),
            monaco(monacoConfig),
            commonjs(),
            vuetify(),
            babel({
                exclude: `${projectRoot}node_modules/**`,
                extensions: ['.js', '.vue'],
                babelHelpers: 'bundled',
                presets: [['@babel/preset-env', { targets: esbrowserslist }]],
            }),
            copy({
                targets: [
                    { src: `${sharePath}/icons`, dest: 'dist' },
                    { src: `${sharePath}/plugins/vuetifyTheme.js`, dest: 'dist' },
                    { src: `${sharePath}/locales`, dest: 'dist' },
                    {
                        src: `${sharePath}/buildConfig/monaco.js`,
                        dest: 'dist/buildConfig',
                    },
                ],
            }),
            terser(),
            copyrightBanner(),
        ],
    },
]
